#include "TerrainFilesFixtures.h"

using namespace terrainFilesFixtures;

using TerrainFileRecoveryTest = TerrainFilesTest;

TEST_F(TerrainFileRecoveryTest, CrashAfterRegionsBeforeIndexLoadsTheOldState)
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

TEST_F(TerrainFileRecoveryTest, RegionFileReplacedOutOfBandIsRewrittenOnNextSave)
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

TEST_F(TerrainFileRecoveryTest, SameSizeGarbledRegionFileFailsLoadNamingIt)
{
  saveDefault();
  const auto file = regionFile("r.0.0.0.");
  const auto size = std::filesystem::file_size(file);
  garbleKeepingSize(file);
  ASSERT_EQ(std::filesystem::file_size(file), size);

  expectLoadFails(file);
}

TEST_F(TerrainFileRecoveryTest, FileHashMismatchInTheIndexFailsLoadNamingTheFile)
{
  saveDefault();
  const auto file = regionFile("r.0.0.0.");
  ASSERT_TRUE(terrain::loadTerrain(m_root, terrainUuid).description.has_value());

  editIndex([](nlohmann::json& json) {
    for (auto& entry : json["regions"])
    {
      if (entry["region"] == nlohmann::json::array({ 0, 0, 0 }))
      {
        entry["fileHash"] = "0000000000000000";
      }
    }
  });

  expectLoadFails(file);
  EXPECT_NE(terrain::loadTerrain(m_root, terrainUuid).error.find("file hash"), std::string::npos);
}

TEST_F(TerrainFileRecoveryTest, SameSizeGarbledRegionFileIsRewrittenOnNextSave)
{
  saveDefault();
  const auto file = regionFile("r.0.0.0.");
  const auto good = readBytes(file);
  garbleKeepingSize(file);
  ASSERT_NE(readBytes(file), good);
  expectLoadFails(file);

  const auto result = terrain::saveTerrain(m_root, makeDescription(), *makeVolume());
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(result.regionsWritten, 1u);
  EXPECT_EQ(readBytes(file), good);

  const auto loaded = terrain::loadTerrain(m_root, terrainUuid);
  ASSERT_NE(loaded.volume, nullptr) << loaded.error;
  EXPECT_EQ(loaded.volume->chunks().size(), 5u);
}

TEST_F(TerrainFileRecoveryTest, SaveWithoutThePreviousIndexRewritesOnlyDamagedRegions)
{
  const auto volume = makeVolume();
  ASSERT_TRUE(terrain::saveTerrain(m_root, makeDescription(), *volume).ok);

  ASSERT_TRUE(std::filesystem::remove(region("terrain.json")));
  const auto intact = terrain::saveTerrain(m_root, makeDescription(), *volume);
  ASSERT_TRUE(intact.ok) << intact.error;
  EXPECT_EQ(intact.regionsWritten, 0u);
  EXPECT_EQ(intact.regionsRemoved, 0u);
  EXPECT_TRUE(std::filesystem::exists(region("terrain.json")));

  ASSERT_TRUE(std::filesystem::remove(region("terrain.json")));
  garbleKeepingSize(regionFile("r.0.0.0."));
  const auto damaged = terrain::saveTerrain(m_root, makeDescription(), *volume);
  ASSERT_TRUE(damaged.ok) << damaged.error;
  EXPECT_EQ(damaged.regionsWritten, 1u);

  const auto loaded = terrain::loadTerrain(m_root, terrainUuid);
  ASSERT_NE(loaded.volume, nullptr) << loaded.error;
  EXPECT_EQ(loaded.volume->chunks().size(), 5u);
}

TEST_F(TerrainFileRecoveryTest, FailedSaveLeavesThePreviousStateAndALaterSaveSucceeds)
{
  const auto a = makeVolume();
  ASSERT_TRUE(terrain::saveTerrain(m_root, makeDescription(), *a).ok);

  const auto b = makeVolume();
  b->setChunk({ 9, 0, 0 }, denseChunk(99));

  const terrain::ChunkKey changedRegion = terrain::regionOfChunk({ 9, 0, 0 });
  const terrain::HashByChunk hashes = { { { 9, 0, 0 }, b->chunk({ 9, 0, 0 })->hash() } };
  const auto blocked = folder() / terrain::regionFileName(changedRegion, terrain::regionTag(hashes));
  ASSERT_TRUE(std::filesystem::create_directory(blocked));

  const auto failed = terrain::saveTerrain(m_root, makeDescription(), *b);
  EXPECT_FALSE(failed.ok);
  EXPECT_NE(failed.error.find(pathToUtf8(blocked)), std::string::npos) << failed.error;

  const auto loadedA = terrain::loadTerrain(m_root, terrainUuid);
  ASSERT_NE(loadedA.volume, nullptr) << loadedA.error;
  ASSERT_EQ(loadedA.volume->chunks().size(), a->chunks().size());

  for (const auto& [coord, chunk] : a->chunks())
  {
    ASSERT_NE(loadedA.volume->chunk(coord), nullptr);
    EXPECT_EQ(loadedA.volume->chunk(coord)->hash(), chunk->hash());
  }

  ASSERT_TRUE(std::filesystem::remove(blocked));

  const auto retried = terrain::saveTerrain(m_root, makeDescription(), *b);
  ASSERT_TRUE(retried.ok) << retried.error;

  const auto loadedB = terrain::loadTerrain(m_root, terrainUuid);
  ASSERT_NE(loadedB.volume, nullptr) << loadedB.error;
  EXPECT_EQ(loadedB.volume->chunk({ 9, 0, 0 })->hash(), b->chunk({ 9, 0, 0 })->hash());
  EXPECT_EQ(loadedB.volume->chunk({ 0, 0, 0 })->hash(), b->chunk({ 0, 0, 0 })->hash());
}

TEST_F(TerrainFileRecoveryTest, SaveRemovesOwnTemporariesAndKeepsForeignFiles)
{
  saveDefault();

  const char* removed[] = { "r.9.9.9.0123456789abcdef.bin.tmp" };
  const char* kept[] = { "notes.bin", "r.notes.bin", "r.1.2.3.xyz.bin", "r.1.2.3.0123456789ABCDEF.bin",
                         "r.1.2.0123456789abcdef.bin", "keep.tmp", "r.1.2.3.0123456789abcdef.bin.bak" };

  for (const char* name : removed)
  {
    writeBytes(region(name), { 7 });
  }

  writeBytes(region("terrain.json.tmp"), { 7 });

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

  EXPECT_FALSE(std::filesystem::exists(region("terrain.json.tmp")));

  for (const char* name : kept)
  {
    EXPECT_TRUE(std::filesystem::exists(region(name))) << name;
  }

  EXPECT_TRUE(terrain::loadTerrain(m_root, terrainUuid).description.has_value());
}

TEST_F(TerrainFileRecoveryTest, ObsoleteVersionedFilesAreRemovedAfterSave)
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
