#include <gtest/gtest.h>

#include "TerrainCreateFlow.h"

#include "TestScene.h"
#include "PhysicsSystem.h"
#include "Replication.h"
#include "WorldUnits.h"
#include "assets/AssetRegistry.h"
#include "edits/RecordEdits.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/collisions/Terrain.h"
#include "scenes/SceneManager.h"
#include "terrain/TerrainAddition.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainReplication.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainUpload.h"
#include "terrain/TerrainVolume.h"
#include "ComponentRegistration.h"
#include "ComponentRegistry.h"

#include <Protocol.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <uuid.h>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

static_assert(worldUnitsPerMeter == PhysicsSystem::unitsPerMeter);

namespace {
  uuids::uuid uuidFrom(const std::string& text)
  {
    return uuids::uuid::from_string(text).value();
  }

  const auto existingUUID = uuidFrom("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
  const auto newTerrainUUID = uuidFrom("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");

  void addTerrainRecord(AssetRegistry& registry, const uuids::uuid& uuid, const std::string& displayName)
  {
    registry.registerAsset({ .uuid = uuid, .type = AssetType::Terrain, .path = terrainAssetPath(uuid),
                             .displayName = displayName });
  }

  AssetRegistry registryWithExistingTerrain()
  {
    AssetRegistry registry;
    addTerrainRecord(registry, existingUUID, "Existing");

    return registry;
  }

  TerrainCreateRequest validFlat()
  {
    TerrainCreateRequest request;
    request.name = "Ground";

    return request;
  }

  // Scratch files live in a per-test folder that is removed with the fixture.
  class TerrainCreateHeightmap : public ::testing::Test
  {
  protected:
    void SetUp() override
    {
      m_dir = std::filesystem::temp_directory_path() /
              ("ecs3d_terrain_create_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
      std::filesystem::create_directories(m_dir);
    }

    void TearDown() override
    {
      std::error_code ec;
      std::filesystem::remove_all(m_dir, ec);
    }

    [[nodiscard]] std::string path(const std::string& name) const
    {
      return (m_dir / name).string();
    }

    std::string writeBytes(const std::string& name, const std::vector<uint8_t>& bytes) const
    {
      std::ofstream out(path(name), std::ios::binary);
      out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

      return path(name);
    }

    // An 8-bit PNG with the given channel count (1 = gray, 3 = RGB).
    std::string writePng8(const std::string& name, const int width, const int height, const int channels,
                          const std::vector<uint8_t>& samples) const
    {
      std::vector<uint8_t> out;
      const auto append = [](void* context, void* data, const int size) {
        auto* bytes = static_cast<std::vector<uint8_t>*>(context);
        const auto* begin = static_cast<const uint8_t*>(data);
        bytes->insert(bytes->end(), begin, begin + size);
      };
      EXPECT_NE(stbi_write_png_to_func(append, &out, width, height, channels, samples.data(), width * channels), 0);

      return writeBytes(name, out);
    }

    // A 16-bit grayscale PNG built by hand (stb writes only 8-bit): stored deflate blocks over big-endian samples.
    std::string writePng16(const std::string& name, const int width, const int height,
                           const std::vector<uint16_t>& samples) const
    {
      std::vector<uint8_t> raw;
      for (int row = 0; row < height; ++row)
      {
        raw.push_back(0);
        for (int column = 0; column < width; ++column)
        {
          const uint16_t value = samples[static_cast<size_t>(row) * width + column];
          raw.push_back(static_cast<uint8_t>(value >> 8));
          raw.push_back(static_cast<uint8_t>(value & 0xff));
        }
      }

      std::vector<uint8_t> zlib = { 0x78, 0x01 };
      for (size_t offset = 0; offset < raw.size() || offset == 0;)
      {
        const size_t length = std::min<size_t>(65535, raw.size() - offset);
        const bool last = offset + length >= raw.size();
        zlib.push_back(last ? 1 : 0);
        zlib.push_back(static_cast<uint8_t>(length & 0xff));
        zlib.push_back(static_cast<uint8_t>(length >> 8));
        zlib.push_back(static_cast<uint8_t>(~length & 0xff));
        zlib.push_back(static_cast<uint8_t>((~length >> 8) & 0xff));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                    raw.begin() + static_cast<std::ptrdiff_t>(offset + length));
        offset += length;

        if (last)
        {
          break;
        }
      }

      uint32_t a = 1;
      uint32_t b = 0;
      for (const uint8_t byte : raw)
      {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
      }
      appendBigEndian(zlib, (b << 16) | a);

      std::vector<uint8_t> png = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
      std::vector<uint8_t> header;
      appendBigEndian(header, static_cast<uint32_t>(width));
      appendBigEndian(header, static_cast<uint32_t>(height));
      header.insert(header.end(), { 16, 0, 0, 0, 0 });
      appendChunk(png, "IHDR", header);
      appendChunk(png, "IDAT", zlib);
      appendChunk(png, "IEND", {});

      return writeBytes(name, png);
    }

  private:
    static void appendBigEndian(std::vector<uint8_t>& bytes, const uint32_t value)
    {
      for (int shift = 24; shift >= 0; shift -= 8)
      {
        bytes.push_back(static_cast<uint8_t>((value >> shift) & 0xff));
      }
    }

    static uint32_t crc32(const std::vector<uint8_t>& bytes)
    {
      uint32_t crc = 0xffffffffu;
      for (const uint8_t byte : bytes)
      {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit)
        {
          crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
      }

      return ~crc;
    }

    static void appendChunk(std::vector<uint8_t>& png, const std::string& type, const std::vector<uint8_t>& data)
    {
      appendBigEndian(png, static_cast<uint32_t>(data.size()));
      std::vector<uint8_t> body(type.begin(), type.end());
      body.insert(body.end(), data.begin(), data.end());
      png.insert(png.end(), body.begin(), body.end());
      appendBigEndian(png, crc32(body));
    }

    std::filesystem::path m_dir;
  };

  
}

TEST(TerrainCreateValidation, AValidRequestIsAccepted)
{
  EXPECT_FALSE(terrainCreate::refusal(validFlat(), registryWithExistingTerrain()).has_value());
}

TEST(TerrainCreateValidation, ANameThatIsEmptyBlankOrHasPathCharactersIsRefused)
{
  const auto registry = registryWithExistingTerrain();

  for (const char* name : { "", "   ", "a/b", "a\\b", "what?" })
  {
    auto request = validFlat();
    request.name = name;
    EXPECT_TRUE(terrainCreate::refusal(request, registry).has_value()) << "[" << name << "]";
  }

  auto fine = validFlat();
  fine.name = "Hills 2";
  EXPECT_FALSE(terrainCreate::refusal(fine, registry).has_value());
}

TEST(TerrainCreateValidation, AnotherTerrainsDisplayNameIsRefusedButOtherAssetTypesDoNotClash)
{
  auto registry = registryWithExistingTerrain();
  registry.registerAsset({ .uuid = uuidFrom("cccccccc-cccc-4ccc-8ccc-cccccccccccc"), .type = AssetType::Prefab,
                           .path = "Ground", .body = "{}" });

  auto taken = validFlat();
  taken.name = "Existing";
  const auto reason = terrainCreate::refusal(taken, registry);
  ASSERT_TRUE(reason.has_value());
  EXPECT_NE(reason->find("Existing"), std::string::npos);

  // The control: a prefab called Ground does not stop a terrain called Ground, and a free name passes.
  EXPECT_FALSE(terrainCreate::refusal(validFlat(), registry).has_value());
}

TEST(TerrainCreateValidation, NonFiniteAndNonPositiveNumbersAreRefused)
{
  const auto registry = registryWithExistingTerrain();
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();

  for (const float bad : { nan, inf, -inf, 0.0f, -1.0f })
  {
    auto footprint = validFlat();
    footprint.footprintMeters = bad;
    EXPECT_TRUE(terrainCreate::refusal(footprint, registry).has_value()) << bad;

    auto voxel = validFlat();
    voxel.voxelSizeMeters = bad;
    EXPECT_TRUE(terrainCreate::refusal(voxel, registry).has_value()) << bad;

    auto height = validFlat();
    height.fromHeightmap = true;
    height.heightmapPath = "image.png";
    height.heightScaleMeters = bad;
    EXPECT_TRUE(terrainCreate::refusal(height, registry).has_value()) << bad;
  }

  auto heightmap = validFlat();
  heightmap.fromHeightmap = true;
  heightmap.heightmapPath = "image.png";
  EXPECT_FALSE(terrainCreate::refusal(heightmap, registry).has_value());

  heightmap.heightmapPath.clear();
  EXPECT_TRUE(terrainCreate::refusal(heightmap, registry).has_value());
}

TEST(TerrainCreateValidation, TheVoxelSizeIsBoundedAfterTheUnitConversion)
{
  const auto registry = registryWithExistingTerrain();

  auto request = validFlat();
  request.voxelSizeMeters = terrain::minVoxelSize / worldUnitsPerMeter * 0.5f;
  EXPECT_TRUE(terrainCreate::refusal(request, registry).has_value());

  request.voxelSizeMeters = terrain::maxVoxelSize / worldUnitsPerMeter * 2.0f;
  EXPECT_TRUE(terrainCreate::refusal(request, registry).has_value());

  // The control: a size just inside each bound passes the voxel check (the footprint is raised to hold a voxel).
  request.voxelSizeMeters = terrain::minVoxelSize / worldUnitsPerMeter * 2.0f;
  request.footprintMeters = 1.0f;
  EXPECT_FALSE(terrainCreate::refusal(request, registry).has_value());

  request.voxelSizeMeters = terrain::maxVoxelSize / worldUnitsPerMeter * 0.5f;
  request.footprintMeters = request.voxelSizeMeters * 4.0f;
  EXPECT_FALSE(terrainCreate::refusal(request, registry).has_value());
}

TEST(TerrainCreateValidation, AFootprintSmallerThanOneVoxelIsRefused)
{
  auto request = validFlat();
  request.footprintMeters = 0.25f;
  EXPECT_TRUE(terrainCreate::refusal(request, registryWithExistingTerrain()).has_value());

  request.footprintMeters = 0.5f;
  EXPECT_FALSE(terrainCreate::refusal(request, registryWithExistingTerrain()).has_value());
}

TEST(TerrainCreateValidation, AFootprintPastTheChunkBoundIsRefusedBeforeAnythingIsGenerated)
{
  const auto registry = registryWithExistingTerrain();

  // A flat terrain at the default voxel size is one chunk tall: 4096 columns a side is exactly the bound.
  auto atBound = validFlat();
  atBound.footprintMeters = 4096.0f * atBound.voxelSizeMeters;
  EXPECT_FALSE(terrainCreate::refusal(atBound, registry).has_value());

  auto past = validFlat();
  past.footprintMeters = 4160.0f * past.voxelSizeMeters;
  const auto reason = terrainCreate::refusal(past, registry);
  ASSERT_TRUE(reason.has_value());
  EXPECT_NE(reason->find("chunks"), std::string::npos);

  const auto generated = terrainCreate::generateTerrain(past, registry, newTerrainUUID);
  EXPECT_FALSE(generated.terrain.has_value());
  EXPECT_FALSE(generated.error.empty());
}

TEST(TerrainCreateUnits, SizesConvertAtTheEnginesUnitsPerMeter)
{
  EXPECT_FLOAT_EQ(terrainCreate::worldUnits(1.0f), worldUnitsPerMeter);
  EXPECT_FLOAT_EQ(terrainCreate::worldUnits(0.5f), 0.5f * worldUnitsPerMeter);

  EXPECT_EQ(terrainCreate::columnsFor(128.0f, 0.5f), 256);
  EXPECT_EQ(terrainCreate::columnsFor(0.1f, 0.5f), 1);
  EXPECT_EQ(terrainCreate::columnsFor(0.0f, 0.5f), 0);
  EXPECT_EQ(terrainCreate::columnsFor(128.0f, std::numeric_limits<float>::quiet_NaN()), 0);
}

TEST(TerrainCreateGenerate, AFlatTerrainHasTheRequestedVoxelSizeAChunkCountAndAFlatSurface)
{
  const auto registry = registryWithExistingTerrain();
  const auto result = terrainCreate::generateTerrain(validFlat(), registry, newTerrainUUID);

  ASSERT_TRUE(result.terrain.has_value()) << result.error;
  const auto& generated = *result.terrain;

  EXPECT_EQ(generated.description.uuid, newTerrainUUID);
  EXPECT_FLOAT_EQ(generated.description.voxelSize, 0.5f * worldUnitsPerMeter);
  EXPECT_TRUE(generated.description.palette.empty());
  EXPECT_TRUE(terrain::isValidDescription(generated.description));
  EXPECT_EQ(generated.displayName, "Ground");

  ASSERT_NE(generated.volume, nullptr);
  EXPECT_FLOAT_EQ(generated.volume->voxelSize(), generated.description.voxelSize);
  EXPECT_GT(generated.description.chunkCount, 0u);
  EXPECT_EQ(generated.description.chunkCount, generated.volume->chunks().size());

  // 256 columns a side at 32 per chunk, one chunk tall.
  EXPECT_EQ(generated.volume->chunks().size(), 64u);

  const int ground = static_cast<int>(terrainCreate::flatGroundVoxels);
  for (const int x : { 0, 100, 255 })
  {
    EXPECT_LT(generated.volume->distanceAtSample({ x, ground - 2, 17 }), 0) << x;
    EXPECT_GT(generated.volume->distanceAtSample({ x, ground + 2, 17 }), 0) << x;
  }

  // Beyond the footprint nothing was written.
  EXPECT_EQ(generated.volume->distanceAtSample({ 300, ground - 2, 17 }), terrain::airDistance);
}

TEST(TerrainCreateGenerate, ARefusedRequestGeneratesNothing)
{
  const auto registry = registryWithExistingTerrain();

  auto taken = validFlat();
  taken.name = "Existing";
  const auto refused = terrainCreate::generateTerrain(taken, registry, newTerrainUUID);
  EXPECT_FALSE(refused.terrain.has_value());
  EXPECT_FALSE(refused.error.empty());

  EXPECT_TRUE(terrainCreate::generateTerrain(validFlat(), registry, newTerrainUUID).terrain.has_value());
}

TEST_F(TerrainCreateHeightmap, AnEightBitImageDecodesScaledToSixteenBits)
{
  const auto file = writePng8("eight.png", 4, 2, 1, { 0, 1, 128, 255, 10, 20, 30, 40 });

  const auto decoded = terrainCreate::decodeHeightmap(file, 1000);
  ASSERT_TRUE(decoded.image.has_value()) << decoded.error;
  EXPECT_EQ(decoded.image->width, 4);
  EXPECT_EQ(decoded.image->height, 2);

  const std::vector<uint16_t> expected = { 0, 257, 128 * 257, 65535, 10 * 257, 20 * 257, 30 * 257, 40 * 257 };
  EXPECT_EQ(decoded.image->pixels, expected);
}

TEST_F(TerrainCreateHeightmap, ASixteenBitImageKeepsItsValues)
{
  const std::vector<uint16_t> samples = { 0, 1, 255, 256, 40000, 65534, 65535, 12345, 777 };
  const auto file = writePng16("sixteen.png", 3, 3, samples);

  const auto decoded = terrainCreate::decodeHeightmap(file, 1000);
  ASSERT_TRUE(decoded.image.has_value()) << decoded.error;
  EXPECT_EQ(decoded.image->width, 3);
  EXPECT_EQ(decoded.image->height, 3);
  EXPECT_EQ(decoded.image->pixels, samples);
}

TEST_F(TerrainCreateHeightmap, AColorImageIsConvertedToGray)
{
  const auto file = writePng8("color.png", 2, 1, 3, { 255, 255, 255, 0, 0, 0 });

  const auto decoded = terrainCreate::decodeHeightmap(file, 1000);
  ASSERT_TRUE(decoded.image.has_value()) << decoded.error;
  ASSERT_EQ(decoded.image->pixels.size(), 2u);
  EXPECT_EQ(decoded.image->pixels[0], 65535);
  EXPECT_EQ(decoded.image->pixels[1], 0);
}

TEST_F(TerrainCreateHeightmap, AnImageOverTheSampleLimitIsRefusedBeforeItIsDecoded)
{
  const auto file = writePng8("big.png", 64, 64, 1, std::vector<uint8_t>(64 * 64, 7));

  const auto refused = terrainCreate::decodeHeightmap(file, 64 * 64 - 1);
  EXPECT_FALSE(refused.image.has_value());
  EXPECT_NE(refused.error.find("64 x 64"), std::string::npos);

  const auto accepted = terrainCreate::decodeHeightmap(file, 64 * 64);
  ASSERT_TRUE(accepted.image.has_value()) << accepted.error;
  EXPECT_EQ(accepted.image->pixels.size(), 64u * 64u);
}

TEST_F(TerrainCreateHeightmap, AMissingEmptyOrNonImageFileIsRefused)
{
  EXPECT_FALSE(terrainCreate::decodeHeightmap(path("missing.png"), 1000).image.has_value());
  EXPECT_FALSE(terrainCreate::decodeHeightmap(writeBytes("empty.png", {}), 1000).image.has_value());

  const std::string text = "this is not an image";
  const auto notImage = writeBytes("text.png", std::vector<uint8_t>(text.begin(), text.end()));
  const auto refused = terrainCreate::decodeHeightmap(notImage, 1000);
  EXPECT_FALSE(refused.image.has_value());
  EXPECT_FALSE(refused.error.empty());

  EXPECT_TRUE(terrainCreate::decodeHeightmap(writePng8("ok.png", 2, 2, 1, { 1, 2, 3, 4 }), 1000).image.has_value());
}

TEST_F(TerrainCreateHeightmap, AHeightmapTerrainFollowsTheImageAndItsHeights)
{
  // Black on the left half of a 64 x 64 image, white on the right.
  std::vector<uint8_t> samples(64 * 64);
  for (int row = 0; row < 64; ++row)
  {
    for (int column = 0; column < 64; ++column)
    {
      samples[static_cast<size_t>(row) * 64 + column] = column < 32 ? 0 : 255;
    }
  }

  auto request = validFlat();
  request.fromHeightmap = true;
  request.heightmapPath = writePng8("steps.png", 64, 64, 1, samples);

  const auto result = terrainCreate::generateTerrain(request, registryWithExistingTerrain(), newTerrainUUID);
  ASSERT_TRUE(result.terrain.has_value()) << result.error;
  const auto& volume = *result.terrain->volume;

  EXPECT_EQ(result.terrain->description.chunkCount, volume.chunks().size());
  EXPECT_GT(volume.chunks().size(), 0u);

  // White is the full height scale: 32 m at 0.5 m voxels is 64 voxels.
  EXPECT_LT(volume.distanceAtSample({ 50, 60, 10 }), 0);
  EXPECT_GT(volume.distanceAtSample({ 50, 70, 10 }), 0);

  // Black is at the floor, with the solid layer under it.
  EXPECT_LT(volume.distanceAtSample({ 5, -1, 10 }), 0);
  EXPECT_GT(volume.distanceAtSample({ 5, 3, 10 }), 0);

  // The footprint is the image: nothing beyond its 64 columns.
  EXPECT_EQ(volume.distanceAtSample({ 70, -1, 10 }), terrain::airDistance);
}

TEST_F(TerrainCreateHeightmap, AHeightScaleThatNeedsTooManyChunksIsRefusedAndASmallOneIsNot)
{
  auto request = validFlat();
  request.fromHeightmap = true;
  request.heightmapPath = writePng8("small.png", 8, 8, 1, std::vector<uint8_t>(64, 200));
  const auto registry = registryWithExistingTerrain();

  EXPECT_TRUE(terrainCreate::generateTerrain(request, registry, newTerrainUUID).terrain.has_value());

  request.heightScaleMeters = 1.0e6f;
  const auto tall = terrainCreate::generateTerrain(request, registry, newTerrainUUID);
  EXPECT_FALSE(tall.terrain.has_value());
  EXPECT_NE(tall.error.find("chunks"), std::string::npos);
}

TEST(TerrainCreateFlowState, TheTemplateGateAsksForSaveAsOnlyWhenWritesAreBlocked)
{
  EXPECT_EQ(TerrainCreateFlow::gate(true), TerrainCreateGate::saveFirst);
  EXPECT_EQ(TerrainCreateFlow::gate(false), TerrainCreateGate::proceed);
}

TEST(TerrainCreateFlowState, BeginOffersAFreeNameOpensOnceAndCancelResetsTheRequest)
{
  auto registry = registryWithExistingTerrain();
  TerrainCreateFlow flow;

  flow.begin(registry);
  EXPECT_EQ(flow.request().name, "New Terrain");
  EXPECT_TRUE(flow.takeOpenRequest());
  EXPECT_FALSE(flow.takeOpenRequest());

  addTerrainRecord(registry, uuidFrom("dddddddd-dddd-4ddd-8ddd-dddddddddddd"), "New Terrain");
  flow.begin(registry);
  EXPECT_EQ(flow.request().name, "New Terrain (2)");

  flow.request().fromHeightmap = true;
  flow.fail("nope");
  EXPECT_EQ(flow.error(), "nope");

  flow.cancel();
  EXPECT_FALSE(flow.request().fromHeightmap);
  EXPECT_TRUE(flow.error().empty());
}

TEST(TerrainCreatePlacement, TheEditAddsARootObjectWithATerrainComponentAndIsRecordedForUndo)
{
  auto scene = fixtures::makeScene();
  const auto objectUUID = uuidFrom("eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee");

  const auto edit = terrainCreate::placementEdit(objectUUID, "Ground", newTerrainUUID);

  const auto commands = edits::commandsForSceneEdit(edit, *scene.objectManager);
  ASSERT_TRUE(commands.has_value());
  EXPECT_EQ(commands->size(), 2u);

  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager, edit), replication::SceneEditResult::applied);

  const auto object = scene.objectManager->getObjectByUUID(objectUUID);
  ASSERT_NE(object, nullptr);
  EXPECT_EQ(object->getName(), "Ground");
  EXPECT_EQ(object->getParent(), nullptr);

  const auto component = object->getComponent<Terrain>(ComponentType::collider);
  ASSERT_NE(component, nullptr);
  EXPECT_EQ(component->getTerrainUUID(), newTerrainUUID);

  // The same edit for an object that already exists is refused, so a repeat cannot add a second terrain object.
  EXPECT_NE(replication::applySceneEdit(*scene.objectManager, edit), replication::SceneEditResult::applied);
}

TEST(TerrainCreateEndToEnd, TheGeneratedTerrainReachesAFreshServerStoreWithMatchingChunks)
{
  // The editor side: the create flow's output through the same addAsset the panel sends.
  auto editorComponents = std::make_shared<ComponentRegistry>();
  registerDataComponents(*editorComponents);
  AssetRegistry editorAssets;
  SceneManager editorScenes;
  terrain::TerrainStore editorStore;

  const auto result = terrainCreate::generateTerrain(validFlat(), editorAssets, newTerrainUUID);
  ASSERT_TRUE(result.terrain.has_value()) << result.error;
  const auto& generated = *result.terrain;

  auto asset = terrain::addTerrainAssetJson(generated.description);
  asset["displayName"] = generated.displayName;

  ASSERT_TRUE(replication::applyAddAssetWithTerrain(editorAssets, editorScenes, editorComponents, editorStore, asset,
                                                    false));
  const auto editorVolume = editorStore.find(newTerrainUUID);
  ASSERT_NE(editorVolume, nullptr);
  EXPECT_TRUE(editorVolume->chunks().empty());
  EXPECT_FALSE(editorStore.expectedChunks(newTerrainUUID).has_value());

  editorVolume->restoreChunks(generated.volume->snapshotChunks());
  EXPECT_EQ(editorVolume->chunks().size(), generated.description.chunkCount);

  const auto* record = editorAssets.getByUUID(newTerrainUUID);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->displayName, "Ground");
  EXPECT_TRUE(terrainCreate::refusal(validFlat(), editorAssets).has_value());

  // The server side: the same addAsset after the wire round trip, which expects the upload.
  auto serverComponents = std::make_shared<ComponentRegistry>();
  registerDataComponents(*serverComponents);
  AssetRegistry serverAssets;
  SceneManager serverScenes;
  terrain::TerrainStore serverStore;

  const auto wired = replication::unpackAddAsset(replication::packAddAsset(asset));
  ASSERT_TRUE(replication::applyAddAssetWithTerrain(serverAssets, serverScenes, serverComponents, serverStore, wired,
                                                    true));
  EXPECT_EQ(serverStore.expectedChunks(newTerrainUUID), std::optional<uint32_t>(generated.description.chunkCount));
  EXPECT_TRUE(terrain::pendingUploadReason(serverStore).has_value());

  terrain::TerrainUpload upload;
  upload.add(newTerrainUUID, generated.volume);

  bool completed = false;
  upload.pump([&](const net::Message& message) {
    const auto payload = terrain::unpackTerrainChunks(message);
    ASSERT_TRUE(payload.has_value());
    completed = terrain::applyTerrainUpload(serverStore, *payload).completed;
  }, 100u * 1024 * 1024);

  EXPECT_TRUE(upload.done());
  EXPECT_TRUE(completed);
  EXPECT_FALSE(terrain::pendingUploadReason(serverStore).has_value());

  const auto serverVolume = serverStore.find(newTerrainUUID);
  ASSERT_NE(serverVolume, nullptr);
  ASSERT_EQ(serverVolume->chunks().size(), generated.volume->chunks().size());

  for (const auto& [coord, chunk] : generated.volume->chunks())
  {
    const auto held = serverVolume->chunk(coord);
    ASSERT_NE(held, nullptr);
    EXPECT_EQ(held->hash(), chunk->hash());

    const auto local = editorVolume->chunk(coord);
    ASSERT_NE(local, nullptr);
    EXPECT_EQ(local->hash(), chunk->hash());
  }
}
