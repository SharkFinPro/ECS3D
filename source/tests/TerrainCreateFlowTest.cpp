#include <gtest/gtest.h>

#include "TerrainCreateFlow.h"

#include "TestScene.h"
#include "TerrainCreateTestHelpers.h"
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
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <uuid.h>
#include <vector>

static_assert(worldUnitsPerMeter == PhysicsSystem::unitsPerMeter);

using namespace terrainCreateTest;

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
  request.footprintMeters = 0.5f;
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

  // A flat terrain at the default voxel size is one chunk tall: 2048 columns a side is exactly 4096 chunks.
  auto big = validFlat();
  big.footprintMeters = 512.0f;
  EXPECT_FALSE(terrainCreate::refusal(big, registry).has_value());

  auto atBound = validFlat();
  atBound.footprintMeters = 2048.0f * atBound.voxelSizeMeters;
  EXPECT_FALSE(terrainCreate::refusal(atBound, registry).has_value());

  auto past = validFlat();
  past.footprintMeters = 2112.0f * past.voxelSizeMeters;
  const auto reason = terrainCreate::refusal(past, registry);
  ASSERT_TRUE(reason.has_value());
  EXPECT_NE(reason->find("chunks"), std::string::npos);
  EXPECT_NE(reason->find("4096"), std::string::npos);

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
  ASSERT_EQ(generated.description.palette.size(), 1u);
  EXPECT_EQ(generated.description.palette[0].name, "Ground");
  EXPECT_EQ(generated.description.palette[0].color, glm::vec3(0.6f));
  EXPECT_EQ(generated.description.palette[0].color, vke::s_defaultVolumeColors[0]);
  EXPECT_TRUE(generated.description.palette[0].albedoTexture.is_nil());
  EXPECT_TRUE(generated.description.palette[0].specularTexture.is_nil());
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

TEST(TerrainCreateEstimate, TheFormShowsTheChunkCountAndAMemoryCeiling)
{
  auto request = validFlat();
  request.footprintMeters = 512.0f;
  const auto text = terrainCreate::estimateText(request);
  EXPECT_NE(text.find("1024 chunks"), std::string::npos) << text;
  EXPECT_NE(text.find("MB"), std::string::npos);

  request.fromHeightmap = true;
  EXPECT_NE(terrainCreate::estimateText(request).find("4096"), std::string::npos);

  request.fromHeightmap = false;
  request.footprintMeters = std::numeric_limits<float>::quiet_NaN();
  EXPECT_TRUE(terrainCreate::estimateText(request).empty());
}

TEST(TerrainCreateValidation, TheVoxelSizeRefusalNamesTheBoundsWithoutTrailingZeros)
{
  auto request = validFlat();
  request.voxelSizeMeters = 0.0f;
  const auto reason = terrainCreate::refusal(request, registryWithExistingTerrain());
  ASSERT_TRUE(reason.has_value());
  EXPECT_NE(reason->find("0.0002 and 200 meters"), std::string::npos) << *reason;
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

namespace {
  struct EditorSide
  {
    std::shared_ptr<ComponentRegistry> components = std::make_shared<ComponentRegistry>();
    AssetRegistry assets;
    SceneManager scenes;
    terrain::TerrainStore store;
  };

  // The create flow's output through the same addAsset the panel sends.
  nlohmann::json addGeneratedTerrain(EditorSide& editor, const GeneratedTerrain& generated)
  {
    registerDataComponents(*editor.components);

    auto asset = terrain::addTerrainAssetJson(generated.description);
    asset["displayName"] = generated.displayName;

    EXPECT_TRUE(replication::applyAddAssetWithTerrain(editor.assets, editor.scenes, editor.components, editor.store,
                                                      asset, false));
    return asset;
  }

  // The same addAsset after the wire round trip, which expects the upload.
  void uploadToFreshServer(terrain::TerrainStore& serverStore, const nlohmann::json& asset,
                           const GeneratedTerrain& generated)
  {
    auto serverComponents = std::make_shared<ComponentRegistry>();
    registerDataComponents(*serverComponents);
    AssetRegistry serverAssets;
    SceneManager serverScenes;

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
  }
}

TEST(TerrainCreateEndToEnd, TheGeneratedTerrainReachesAFreshServerStoreWithMatchingChunks)
{
  EditorSide editor;
  const auto result = terrainCreate::generateTerrain(validFlat(), editor.assets, newTerrainUUID);
  ASSERT_TRUE(result.terrain.has_value()) << result.error;
  const auto& generated = *result.terrain;

  const auto asset = addGeneratedTerrain(editor, generated);
  const auto editorVolume = editor.store.find(newTerrainUUID);
  ASSERT_NE(editorVolume, nullptr);
  EXPECT_TRUE(editorVolume->chunks().empty());
  EXPECT_FALSE(editor.store.expectedChunks(newTerrainUUID).has_value());

  editorVolume->restoreChunks(generated.volume->snapshotChunks());
  EXPECT_EQ(editorVolume->chunks().size(), generated.description.chunkCount);

  const auto* record = editor.assets.getByUUID(newTerrainUUID);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->displayName, "Ground");
  EXPECT_TRUE(terrainCreate::refusal(validFlat(), editor.assets).has_value());

  terrain::TerrainStore serverStore;
  uploadToFreshServer(serverStore, asset, generated);

  const auto serverVolume = serverStore.find(newTerrainUUID);
  ASSERT_NE(serverVolume, nullptr);
  ASSERT_EQ(serverVolume->chunks().size(), generated.volume->chunks().size());
  ASSERT_NE(serverStore.description(newTerrainUUID), nullptr);
  EXPECT_EQ(serverStore.description(newTerrainUUID)->palette, generated.description.palette);
  EXPECT_EQ(editor.store.description(newTerrainUUID)->palette, generated.description.palette);

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
