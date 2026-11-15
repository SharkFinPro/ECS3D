#include "TerrainFilesFixtures.h"

using namespace terrainFilesFixtures;

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

  expectErrorMentions(terrain::loadTerrain(m_root, uuids::uuid()), "uuid is nil");
  expectErrorMentions(terrain::loadTerrain({}, terrainUuid), "root is empty");
}

TEST_F(TerrainFilesTest, SaveRefusesBadInputsNamingTheCondition)
{
  const auto volume = makeVolume();

  auto nilDescription = makeDescription();
  nilDescription.uuid = uuids::uuid();
  const auto nil = terrain::saveTerrain(m_root, nilDescription, *volume);
  EXPECT_FALSE(nil.ok);
  EXPECT_NE(nil.error.find("uuid is nil"), std::string::npos) << nil.error;

  const auto empty = terrain::saveTerrain({}, makeDescription(), *volume);
  EXPECT_FALSE(empty.ok);
  EXPECT_NE(empty.error.find("root is empty"), std::string::npos) << empty.error;

  auto invalid = makeDescription();
  invalid.voxelSize = 0.0f;
  const auto invalidResult = terrain::saveTerrain(m_root, invalid, *volume);
  EXPECT_FALSE(invalidResult.ok);
  EXPECT_NE(invalidResult.error.find("description is invalid"), std::string::npos) << invalidResult.error;

  const auto mismatch = terrain::saveTerrain(m_root, makeDescription(1.0f), *volume);
  EXPECT_FALSE(mismatch.ok);
  EXPECT_NE(mismatch.error.find("voxelSize does not match"), std::string::npos) << mismatch.error;
  EXPECT_FALSE(std::filesystem::exists(folder()));

  EXPECT_TRUE(terrain::saveTerrain(m_root, makeDescription(), *volume).ok);
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
