#include <gtest/gtest.h>

#include "ProjectPaths.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainFiles.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {
  const uuids::uuid terrainUuid = uuids::uuid::from_string("0A1B2C3D-0000-4000-8000-00000000ABCD").value();

  std::shared_ptr<const terrain::TerrainChunk> denseChunk(const uint32_t seed)
  {
    std::mt19937 rng(seed);
    std::vector<int16_t> distances(terrain::samplesPerChunk);
    std::vector<uint8_t> materials(terrain::samplesPerChunk);

    for (size_t i = 0; i < distances.size(); ++i)
    {
      distances[i] = static_cast<int16_t>(static_cast<int>(rng() % (2 * terrain::airDistance + 1)) -
                                          terrain::airDistance);
      materials[i] = static_cast<uint8_t>(rng() % 4);
    }

    return terrain::TerrainChunk::dense(std::move(distances), std::move(materials));
  }

  std::shared_ptr<const terrain::TerrainChunk> solidChunk()
  {
    return terrain::TerrainChunk::uniform(-terrain::airDistance, 1);
  }

  terrain::TerrainDescription makeDescription(const float voxelSize = 2.5f)
  {
    terrain::TerrainDescription description;
    description.uuid = terrainUuid;
    description.voxelSize = voxelSize;
    description.palette.push_back({ "grass", terrainUuid, uuids::uuid() });
    description.palette.push_back({ "rock", uuids::uuid(), terrainUuid });
    return description;
  }

  // Five chunks in four regions: (0,0,0) holds two, (-1,-1,-1), (1,0,0) and (-2,0,0) one each.
  std::shared_ptr<terrain::TerrainVolume> makeVolume(const float voxelSize = 2.5f)
  {
    auto volume = std::make_shared<terrain::TerrainVolume>(voxelSize);
    volume->setChunk({ 0, 0, 0 }, denseChunk(1));
    volume->setChunk({ 1, 0, 0 }, solidChunk());
    volume->setChunk({ -1, -1, -1 }, denseChunk(2));
    volume->setChunk({ 9, 0, 0 }, denseChunk(3));
    volume->setChunk({ -9, 2, 3 }, solidChunk());
    return volume;
  }

  std::vector<uint8_t> readBytes(const std::filesystem::path& file)
  {
    std::ifstream in(file, std::ios::binary);
    return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
  }

  void writeBytes(const std::filesystem::path& file, const std::vector<uint8_t>& bytes)
  {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }

  void expectErrorMentions(const terrain::TerrainLoadResult& loaded, const char* text)
  {
    EXPECT_FALSE(loaded.description.has_value());
    EXPECT_EQ(loaded.volume, nullptr);
    EXPECT_NE(loaded.error.find(text), std::string::npos) << loaded.error;
  }

  class TerrainFilesTest : public testing::Test
  {
  protected:
    void SetUp() override
    {
      const auto* info = testing::UnitTest::GetInstance()->current_test_info();
      std::random_device device;
      m_root = std::filesystem::temp_directory_path() /
               (std::string("ecs3d_terrain_files_") + info->name() + "_" + std::to_string(device()));
      std::filesystem::remove_all(m_root);
      std::filesystem::create_directories(m_root);
    }

    void TearDown() override
    {
      std::error_code ignored;
      std::filesystem::remove_all(m_root, ignored);
    }

    [[nodiscard]] std::filesystem::path folder() const
    {
      return terrain::terrainFolder(m_root, terrainUuid);
    }

    [[nodiscard]] std::filesystem::path region(const char* name) const
    {
      return folder() / name;
    }

    // The region file whose name starts with prefix, such as "r.1.0.0.", or an empty path when there is none.
    [[nodiscard]] std::filesystem::path regionFile(const std::string& prefix) const
    {
      for (const auto& entry : std::filesystem::directory_iterator(folder()))
      {
        const std::string name = pathToUtf8(entry.path().filename());

        if (name.starts_with(prefix) && name.ends_with(".bin"))
        {
          return entry.path();
        }
      }

      return {};
    }

    [[nodiscard]] static std::vector<std::string> fileNames(const std::filesystem::path& directory)
    {
      std::vector<std::string> names;

      for (const auto& entry : std::filesystem::directory_iterator(directory))
      {
        names.push_back(pathToUtf8(entry.path().filename()));
      }

      std::sort(names.begin(), names.end());
      return names;
    }

    void saveDefault()
    {
      const auto result = terrain::saveTerrain(m_root, makeDescription(), *makeVolume());
      ASSERT_TRUE(result.ok) << result.error;
    }

    void expectLoadFails(const std::filesystem::path& namedFile)
    {
      const auto loaded = terrain::loadTerrain(m_root, terrainUuid);
      EXPECT_FALSE(loaded.description.has_value());
      EXPECT_EQ(loaded.volume, nullptr);
      EXPECT_FALSE(loaded.error.empty());
      EXPECT_NE(loaded.error.find(pathToUtf8(namedFile)), std::string::npos) << loaded.error;
    }

    void editIndex(const std::function<void(nlohmann::json&)>& edit)
    {
      const auto file = folder() / "terrain.json";
      auto json = nlohmann::json::parse(readBytes(file));
      edit(json);
      const std::string text = json.dump();
      writeBytes(file, std::vector<uint8_t>(text.begin(), text.end()));
    }

    std::filesystem::path m_root;
  };
}

TEST_F(TerrainFilesTest, FolderIsRootTerrainLowercaseUuid)
{
  EXPECT_EQ(terrain::terrainFolder(m_root, terrainUuid), m_root / "terrain" / "0a1b2c3d-0000-4000-8000-00000000abcd");
}

TEST_F(TerrainFilesTest, RoundTripIsBitExact)
{
  const auto source = makeVolume();
  const auto saved = terrain::saveTerrain(m_root, makeDescription(), *source);
  ASSERT_TRUE(saved.ok) << saved.error;
  EXPECT_EQ(saved.regionsWritten, 4u);
  EXPECT_TRUE(std::filesystem::exists(region("terrain.json")));
  EXPECT_FALSE(regionFile("r.-1.-1.-1.").empty());

  const auto loaded = terrain::loadTerrain(m_root, terrainUuid);
  ASSERT_TRUE(loaded.description.has_value()) << loaded.error;
  ASSERT_NE(loaded.volume, nullptr);

  EXPECT_EQ(loaded.description->uuid, terrainUuid);
  EXPECT_EQ(loaded.description->voxelSize, 2.5f);
  EXPECT_EQ(loaded.description->chunkCount, 5u);
  ASSERT_EQ(loaded.description->palette.size(), 2u);
  EXPECT_EQ(loaded.description->palette[0].name, "grass");
  EXPECT_EQ(loaded.description->palette[0].albedoTexture, terrainUuid);
  EXPECT_TRUE(loaded.description->palette[0].specularTexture.is_nil());
  EXPECT_EQ(loaded.description->palette[1].specularTexture, terrainUuid);

  EXPECT_EQ(loaded.volume->voxelSize(), 2.5f);
  ASSERT_EQ(loaded.volume->chunks().size(), source->chunks().size());

  for (const auto& [coord, chunk] : source->chunks())
  {
    const auto found = loaded.volume->chunk(coord);
    ASSERT_NE(found, nullptr) << coord.x << "," << coord.y << "," << coord.z;
    EXPECT_EQ(found->hash(), chunk->hash());
    EXPECT_EQ(found->isUniform(), chunk->isUniform());
  }
}

TEST_F(TerrainFilesTest, SecondSaveWithoutChangesWritesNothing)
{
  const auto volume = makeVolume();
  const auto first = terrain::saveTerrain(m_root, makeDescription(), *volume);
  ASSERT_TRUE(first.ok) << first.error;
  EXPECT_EQ(first.regionsWritten, 4u);

  const auto second = terrain::saveTerrain(m_root, makeDescription(), *volume);
  ASSERT_TRUE(second.ok) << second.error;
  EXPECT_EQ(second.regionsWritten, 0u);
  EXPECT_EQ(second.regionsRemoved, 0u);
}

TEST_F(TerrainFilesTest, ChangingOneChunkRewritesOnlyItsRegion)
{
  const auto volume = makeVolume();
  ASSERT_TRUE(terrain::saveTerrain(m_root, makeDescription(), *volume).ok);

  const char* untouched[] = { "r.0.0.0.", "r.-1.-1.-1.", "r.-2.0.0." };
  const auto stale = std::filesystem::file_time_type::clock::now() - std::chrono::hours(2);
  std::vector<std::filesystem::path> files;
  std::vector<std::filesystem::file_time_type> staleRead;
  std::vector<std::vector<uint8_t>> before;

  for (const char* prefix : untouched)
  {
    const auto file = regionFile(prefix);
    ASSERT_FALSE(file.empty()) << prefix;
    std::filesystem::last_write_time(file, stale);
    files.push_back(file);
    staleRead.push_back(std::filesystem::last_write_time(file));
    before.push_back(readBytes(file));
  }

  const auto changedBefore = regionFile("r.1.0.0.");
  volume->setChunk({ 9, 0, 0 }, denseChunk(99));

  const auto result = terrain::saveTerrain(m_root, makeDescription(), *volume);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(result.regionsWritten, 1u);
  EXPECT_EQ(result.regionsRemoved, 1u);
  EXPECT_NE(regionFile("r.1.0.0."), changedBefore);
  EXPECT_FALSE(std::filesystem::exists(changedBefore));

  for (size_t i = 0; i < files.size(); ++i)
  {
    EXPECT_EQ(readBytes(files[i]), before[i]) << untouched[i];
    EXPECT_EQ(std::filesystem::last_write_time(files[i]), staleRead[i]) << untouched[i];
  }

  const auto loaded = terrain::loadTerrain(m_root, terrainUuid);
  ASSERT_NE(loaded.volume, nullptr) << loaded.error;
  EXPECT_EQ(loaded.volume->chunk({ 9, 0, 0 })->hash(), volume->chunk({ 9, 0, 0 })->hash());
}

TEST_F(TerrainFilesTest, EmptiedRegionFileIsDeleted)
{
  const auto volume = makeVolume();
  ASSERT_TRUE(terrain::saveTerrain(m_root, makeDescription(), *volume).ok);
  const auto emptied = regionFile("r.-2.0.0.");
  ASSERT_FALSE(emptied.empty());

  volume->setChunk({ -9, 2, 3 }, nullptr);

  const auto result = terrain::saveTerrain(m_root, makeDescription(), *volume);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(result.regionsWritten, 0u);
  EXPECT_EQ(result.regionsRemoved, 1u);
  EXPECT_FALSE(std::filesystem::exists(emptied));
  EXPECT_FALSE(regionFile("r.0.0.0.").empty());

  const auto loaded = terrain::loadTerrain(m_root, terrainUuid);
  ASSERT_NE(loaded.volume, nullptr) << loaded.error;
  EXPECT_EQ(loaded.volume->chunks().size(), 4u);
}

TEST_F(TerrainFilesTest, TruncatedRegionFileFailsNamingIt)
{
  saveDefault();
  const auto file = regionFile("r.0.0.0.");
  auto bytes = readBytes(file);
  ASSERT_GT(bytes.size(), 10u);
  bytes.resize(bytes.size() - 5);
  writeBytes(file, bytes);

  expectLoadFails(file);
}

TEST_F(TerrainFilesTest, MissingRegionFileFails)
{
  saveDefault();
  const auto missing = regionFile("r.-1.-1.-1.");
  ASSERT_TRUE(std::filesystem::remove(missing));

  expectLoadFails(missing);
}

TEST_F(TerrainFilesTest, IndexWithWrongUuidFails)
{
  saveDefault();
  editIndex([](nlohmann::json& json) { json["uuid"] = "11111111-1111-4111-8111-111111111111"; });

  expectLoadFails(region("terrain.json"));
}

TEST_F(TerrainFilesTest, LoadRefusesNilUuidAndEmptyRoot)
{
  saveDefault();

  EXPECT_TRUE(terrain::loadTerrain(m_root, terrainUuid).description.has_value());

  const auto nil = terrain::loadTerrain(m_root, uuids::uuid());
  EXPECT_FALSE(nil.description.has_value());
  EXPECT_FALSE(nil.error.empty());

  const auto empty = terrain::loadTerrain({}, terrainUuid);
  EXPECT_FALSE(empty.description.has_value());
  EXPECT_FALSE(empty.error.empty());
}

TEST_F(TerrainFilesTest, SaveRefusesNilUuidEmptyRootAndVoxelSizeMismatch)
{
  const auto volume = makeVolume();

  auto nilDescription = makeDescription();
  nilDescription.uuid = uuids::uuid();
  EXPECT_FALSE(terrain::saveTerrain(m_root, nilDescription, *volume).ok);

  EXPECT_FALSE(terrain::saveTerrain({}, makeDescription(), *volume).ok);

  const auto mismatch = terrain::saveTerrain(m_root, makeDescription(1.0f), *volume);
  EXPECT_FALSE(mismatch.ok);
  EXPECT_FALSE(mismatch.error.empty());
  EXPECT_FALSE(std::filesystem::exists(folder()));

  EXPECT_TRUE(terrain::saveTerrain(m_root, makeDescription(), *volume).ok);
}

TEST_F(TerrainFilesTest, CrashAfterRegionsBeforeIndexLoadsTheOldState)
{
  const auto a = makeVolume();
  ASSERT_TRUE(terrain::saveTerrain(m_root, makeDescription(), *a).ok);
  const auto namesA = fileNames(folder());

  const auto b = makeVolume();
  b->setChunk({ 9, 0, 0 }, denseChunk(99));
  const auto otherRoot = m_root / "other";
  ASSERT_TRUE(terrain::saveTerrain(otherRoot, makeDescription(), *b).ok);
  const auto otherFolder = terrain::terrainFolder(otherRoot, terrainUuid);

  size_t copied = 0;

  for (const std::string& name : fileNames(otherFolder))
  {
    if (name != "terrain.json" && !std::filesystem::exists(folder() / name))
    {
      std::filesystem::copy_file(otherFolder / name, folder() / name);
      ++copied;
    }
  }

  ASSERT_EQ(copied, 1u);
  ASSERT_EQ(fileNames(folder()).size(), namesA.size() + 1);

  const auto loadedA = terrain::loadTerrain(m_root, terrainUuid);
  ASSERT_NE(loadedA.volume, nullptr) << loadedA.error;
  ASSERT_EQ(loadedA.volume->chunks().size(), a->chunks().size());

  for (const auto& [coord, chunk] : a->chunks())
  {
    ASSERT_NE(loadedA.volume->chunk(coord), nullptr);
    EXPECT_EQ(loadedA.volume->chunk(coord)->hash(), chunk->hash());
  }

  const auto resumed = terrain::saveTerrain(m_root, makeDescription(), *b);
  ASSERT_TRUE(resumed.ok) << resumed.error;
  EXPECT_EQ(resumed.regionsWritten, 0u);
  EXPECT_EQ(resumed.regionsRemoved, 1u);

  const auto loadedB = terrain::loadTerrain(m_root, terrainUuid);
  ASSERT_NE(loadedB.volume, nullptr) << loadedB.error;
  EXPECT_EQ(loadedB.volume->chunk({ 9, 0, 0 })->hash(), b->chunk({ 9, 0, 0 })->hash());
}

TEST_F(TerrainFilesTest, RegionFileReplacedOutOfBandIsRewrittenOnNextSave)
{
  saveDefault();
  const auto file = regionFile("r.0.0.0.");
  writeBytes(file, { 1, 2, 3 });

  expectLoadFails(file);

  const auto result = terrain::saveTerrain(m_root, makeDescription(), *makeVolume());
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(result.regionsWritten, 1u);

  const auto loaded = terrain::loadTerrain(m_root, terrainUuid);
  ASSERT_NE(loaded.volume, nullptr) << loaded.error;
  EXPECT_EQ(loaded.volume->chunks().size(), 5u);
}

TEST_F(TerrainFilesTest, SaveRemovesOwnTemporariesAndKeepsForeignFiles)
{
  saveDefault();

  const char* removed[] = { "terrain.json.tmp", "r.9.9.9.0123456789abcdef.bin.tmp" };
  const char* kept[] = { "notes.bin", "r.notes.bin", "r.1.2.3.xyz.bin", "r.1.2.3.0123456789ABCDEF.bin",
                         "r.1.2.0123456789abcdef.bin", "keep.tmp", "r.1.2.3.0123456789abcdef.bin.bak" };

  for (const char* name : removed)
  {
    writeBytes(region(name), { 7 });
  }

  for (const char* name : kept)
  {
    writeBytes(region(name), { 7 });
  }

  const auto result = terrain::saveTerrain(m_root, makeDescription(), *makeVolume());
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(result.regionsRemoved, 0u);

  for (const char* name : removed)
  {
    EXPECT_FALSE(std::filesystem::exists(region(name))) << name;
  }

  for (const char* name : kept)
  {
    EXPECT_TRUE(std::filesystem::exists(region(name))) << name;
  }

  EXPECT_TRUE(terrain::loadTerrain(m_root, terrainUuid).description.has_value());
}

TEST_F(TerrainFilesTest, ObsoleteVersionedFilesAreRemovedAfterSave)
{
  const auto volume = makeVolume();
  ASSERT_TRUE(terrain::saveTerrain(m_root, makeDescription(), *volume).ok);
  const auto first = regionFile("r.0.0.0.");

  volume->setChunk({ 0, 0, 0 }, denseChunk(50));
  ASSERT_TRUE(terrain::saveTerrain(m_root, makeDescription(), *volume).ok);
  const auto second = regionFile("r.0.0.0.");

  EXPECT_NE(first, second);
  EXPECT_FALSE(std::filesystem::exists(first));
  EXPECT_TRUE(std::filesystem::exists(second));
}

TEST_F(TerrainFilesTest, OversizedIndexIsRefused)
{
  saveDefault();
  writeBytes(region("terrain.json"), std::vector<uint8_t>(9u * 1024 * 1024, ' '));

  expectErrorMentions(terrain::loadTerrain(m_root, terrainUuid), "larger than the format allows");
}

TEST_F(TerrainFilesTest, EmptyVolumeRoundTrips)
{
  const terrain::TerrainVolume empty(2.5f);
  const auto saved = terrain::saveTerrain(m_root, makeDescription(), empty);
  ASSERT_TRUE(saved.ok) << saved.error;
  EXPECT_EQ(saved.regionsWritten, 0u);

  const auto loaded = terrain::loadTerrain(m_root, terrainUuid);
  ASSERT_TRUE(loaded.description.has_value()) << loaded.error;
  ASSERT_NE(loaded.volume, nullptr);
  EXPECT_TRUE(loaded.volume->chunks().empty());
  EXPECT_EQ(loaded.description->chunkCount, 0u);
  EXPECT_EQ(loaded.description->palette.size(), 2u);
}

TEST_F(TerrainFilesTest, VoxelSizeRoundTripsAndReadsCleanlyInTheIndex)
{
  const float sizes[] = { 0.1f, 2.5f, 0.3f, 1.0f / 3.0f };

  for (const float size : sizes)
  {
    ASSERT_TRUE(terrain::saveTerrain(m_root, makeDescription(size), *makeVolume(size)).ok) << size;

    const auto loaded = terrain::loadTerrain(m_root, terrainUuid);
    ASSERT_TRUE(loaded.description.has_value()) << loaded.error;
    EXPECT_EQ(loaded.description->voxelSize, size);
    EXPECT_EQ(loaded.volume->voxelSize(), size);

    const auto json = nlohmann::json::parse(readBytes(region("terrain.json")));
    EXPECT_EQ(static_cast<float>(json["voxelSize"].get<double>()), size);
  }

  ASSERT_TRUE(terrain::saveTerrain(m_root, makeDescription(0.1f), *makeVolume(0.1f)).ok);
  EXPECT_EQ(nlohmann::json::parse(readBytes(region("terrain.json")))["voxelSize"].get<double>(), 0.1);
}
