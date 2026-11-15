#include <gtest/gtest.h>

#include "ProjectPaths.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainFiles.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <nlohmann/json.hpp>

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
  EXPECT_TRUE(std::filesystem::exists(region("r.-1.-1.-1.bin")));

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

  const char* untouched[] = { "r.0.0.0.bin", "r.-1.-1.-1.bin", "r.-2.0.0.bin" };
  const auto stale = std::filesystem::file_time_type::clock::now() - std::chrono::hours(2);
  std::vector<std::vector<uint8_t>> before;

  for (const char* name : untouched)
  {
    std::filesystem::last_write_time(region(name), stale);
    before.push_back(readBytes(region(name)));
  }

  const auto changedBefore = readBytes(region("r.1.0.0.bin"));
  volume->setChunk({ 9, 0, 0 }, denseChunk(99));

  const auto result = terrain::saveTerrain(m_root, makeDescription(), *volume);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(result.regionsWritten, 1u);
  EXPECT_EQ(result.regionsRemoved, 0u);
  EXPECT_NE(readBytes(region("r.1.0.0.bin")), changedBefore);

  for (size_t i = 0; i < before.size(); ++i)
  {
    EXPECT_EQ(readBytes(region(untouched[i])), before[i]) << untouched[i];
    EXPECT_EQ(std::filesystem::last_write_time(region(untouched[i])), stale) << untouched[i];
  }

  const auto loaded = terrain::loadTerrain(m_root, terrainUuid);
  ASSERT_NE(loaded.volume, nullptr) << loaded.error;
  EXPECT_EQ(loaded.volume->chunk({ 9, 0, 0 })->hash(), volume->chunk({ 9, 0, 0 })->hash());
}

TEST_F(TerrainFilesTest, EmptiedRegionFileIsDeleted)
{
  const auto volume = makeVolume();
  ASSERT_TRUE(terrain::saveTerrain(m_root, makeDescription(), *volume).ok);
  ASSERT_TRUE(std::filesystem::exists(region("r.-2.0.0.bin")));

  volume->setChunk({ -9, 2, 3 }, nullptr);

  const auto result = terrain::saveTerrain(m_root, makeDescription(), *volume);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(result.regionsWritten, 0u);
  EXPECT_EQ(result.regionsRemoved, 1u);
  EXPECT_FALSE(std::filesystem::exists(region("r.-2.0.0.bin")));
  EXPECT_TRUE(std::filesystem::exists(region("r.0.0.0.bin")));

  const auto loaded = terrain::loadTerrain(m_root, terrainUuid);
  ASSERT_NE(loaded.volume, nullptr) << loaded.error;
  EXPECT_EQ(loaded.volume->chunks().size(), 4u);
}

TEST_F(TerrainFilesTest, TruncatedRegionFileFailsNamingIt)
{
  saveDefault();
  auto bytes = readBytes(region("r.0.0.0.bin"));
  ASSERT_GT(bytes.size(), 10u);
  bytes.resize(bytes.size() - 5);
  writeBytes(region("r.0.0.0.bin"), bytes);

  expectLoadFails(region("r.0.0.0.bin"));
}

TEST_F(TerrainFilesTest, GarbledMagicFailsNamingTheFile)
{
  saveDefault();
  auto bytes = readBytes(region("r.1.0.0.bin"));
  bytes[0] = 'X';
  writeBytes(region("r.1.0.0.bin"), bytes);

  expectLoadFails(region("r.1.0.0.bin"));
}

TEST_F(TerrainFilesTest, HashDisagreeingWithIndexFails)
{
  saveDefault();
  editIndex([](nlohmann::json& json) {
    for (auto& entry : json["regions"])
    {
      if (entry["region"] == nlohmann::json::array({ 1, 0, 0 }))
      {
        entry["chunks"][0]["hash"] = "0123456789abcdef";
      }
    }
  });

  expectLoadFails(region("r.1.0.0.bin"));
}

TEST_F(TerrainFilesTest, MissingRegionFileFails)
{
  saveDefault();
  ASSERT_TRUE(std::filesystem::remove(region("r.-1.-1.-1.bin")));

  expectLoadFails(region("r.-1.-1.-1.bin"));
}

TEST_F(TerrainFilesTest, IndexWithWrongUuidFails)
{
  saveDefault();
  editIndex([](nlohmann::json& json) { json["uuid"] = "11111111-1111-4111-8111-111111111111"; });

  expectLoadFails(region("terrain.json"));
}

TEST_F(TerrainFilesTest, IndexWithBadVersionFails)
{
  saveDefault();
  editIndex([](nlohmann::json& json) { json["formatVersion"] = terrain::terrainFormatVersion + 1; });

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
