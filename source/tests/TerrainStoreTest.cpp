#include <gtest/gtest.h>

#include "assets/AssetRegistry.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainTypes.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {
  uuids::uuid uuidFrom(const std::string& text)
  {
    return uuids::uuid::from_string(text).value();
  }

  const auto firstUUID = uuidFrom("11111111-1111-1111-1111-111111111111");
  const auto secondUUID = uuidFrom("22222222-2222-2222-2222-222222222222");
  const auto thirdUUID = uuidFrom("33333333-3333-3333-3333-333333333333");

  terrain::TerrainDescription describe(const uuids::uuid& uuid, const float voxelSize = 2.5f)
  {
    terrain::TerrainDescription description;
    description.uuid = uuid;
    description.voxelSize = voxelSize;

    return description;
  }

  AssetRecord terrainRecord(const uuids::uuid& uuid)
  {
    return { .uuid = uuid, .type = AssetType::Terrain, .path = terrainAssetPath(uuid) };
  }

  std::shared_ptr<const terrain::TerrainChunk> solidChunk()
  {
    return terrain::TerrainChunk::uniform(-terrain::airDistance, 1);
  }
}

TEST(TerrainStore, CreateFindDescriptionAndRemove)
{
  terrain::TerrainStore store;
  EXPECT_EQ(store.find(firstUUID), nullptr);
  EXPECT_EQ(store.description(firstUUID), nullptr);
  EXPECT_FALSE(store.remove(firstUUID));

  auto description = describe(firstUUID, 4.0f);
  description.palette.push_back({ "grass", secondUUID, thirdUUID });
  description.chunkCount = 7;

  const auto volume = store.create(description);
  ASSERT_NE(volume, nullptr);
  EXPECT_EQ(store.find(firstUUID), volume);
  EXPECT_NEAR(volume->voxelSize(), 4.0f, 1.0e-6f);

  const auto* stored = store.description(firstUUID);
  ASSERT_NE(stored, nullptr);
  EXPECT_EQ(stored->uuid, firstUUID);
  EXPECT_EQ(stored->chunkCount, 7u);
  ASSERT_EQ(stored->palette.size(), 1u);
  EXPECT_EQ(stored->palette[0].name, "grass");
  EXPECT_EQ(stored->palette[0].albedoTexture, secondUUID);

  EXPECT_TRUE(store.remove(firstUUID));
  EXPECT_EQ(store.find(firstUUID), nullptr);
  EXPECT_TRUE(store.terrains().empty());
}

TEST(TerrainStore, CreateRefusesInvalidDescriptions)
{
  terrain::TerrainStore store;

  EXPECT_EQ(store.create(describe(uuids::uuid{})), nullptr);
  EXPECT_EQ(store.create(describe(firstUUID, std::numeric_limits<float>::quiet_NaN())), nullptr);
  EXPECT_EQ(store.create(describe(firstUUID, std::numeric_limits<float>::infinity())), nullptr);
  EXPECT_EQ(store.create(describe(firstUUID, 0.0f)), nullptr);
  EXPECT_EQ(store.create(describe(firstUUID, -1.0f)), nullptr);
  EXPECT_EQ(store.create(describe(firstUUID, 1.0e6f)), nullptr);

  auto oversized = describe(firstUUID);
  oversized.palette.assign(terrain::maxPaletteSize + 1, terrain::TerrainMaterial{});
  EXPECT_EQ(store.create(oversized), nullptr);

  auto longName = describe(firstUUID);
  longName.palette.push_back({ std::string(terrain::maxMaterialNameLength + 1, 'a'), {}, {} });
  EXPECT_EQ(store.create(longName), nullptr);
  EXPECT_TRUE(store.terrains().empty());

  auto maximal = describe(firstUUID);
  maximal.palette.assign(terrain::maxPaletteSize, terrain::TerrainMaterial{});
  maximal.palette[0].name = std::string(terrain::maxMaterialNameLength, 'a');
  EXPECT_NE(store.create(maximal), nullptr);
}

TEST(TerrainStore, CreateRefusesADuplicateUuidAndLeavesTheFirstVolumeUntouched)
{
  terrain::TerrainStore store;

  const auto first = store.create(describe(firstUUID, 3.0f));
  ASSERT_NE(first, nullptr);
  first->setChunk({ 0, 0, 0 }, solidChunk());

  EXPECT_EQ(store.create(describe(firstUUID, 5.0f)), nullptr);

  EXPECT_EQ(store.find(firstUUID), first);
  EXPECT_NEAR(first->voxelSize(), 3.0f, 1.0e-6f);
  EXPECT_NE(first->chunk({ 0, 0, 0 }), nullptr);
  EXPECT_NEAR(store.description(firstUUID)->voxelSize, 3.0f, 1.0e-6f);

  EXPECT_NE(store.create(describe(secondUUID)), nullptr);
}

TEST(TerrainStore, TerrainsAreSorted)
{
  terrain::TerrainStore store;
  ASSERT_NE(store.create(describe(thirdUUID)), nullptr);
  ASSERT_NE(store.create(describe(firstUUID)), nullptr);
  ASSERT_NE(store.create(describe(secondUUID)), nullptr);

  const auto terrains = store.terrains();
  ASSERT_EQ(terrains.size(), 3u);
  EXPECT_TRUE(std::is_sorted(terrains.begin(), terrains.end()));
  EXPECT_EQ(terrains.front(), firstUUID);
  EXPECT_EQ(terrains.back(), thirdUUID);
}

TEST(TerrainStore, ReconcileCreatesDefaultTerrainsForNewRecords)
{
  AssetRegistry registry;
  registry.registerAsset(terrainRecord(firstUUID));
  registry.registerAsset({ .uuid = secondUUID, .type = AssetType::Model, .path = "rock.glb" });

  terrain::TerrainStore store;
  store.reconcile(registry);

  const auto volume = store.find(firstUUID);
  ASSERT_NE(volume, nullptr);
  EXPECT_TRUE(volume->chunks().empty());
  const terrain::TerrainDescription defaults;
  EXPECT_NEAR(volume->voxelSize(), defaults.voxelSize, 1.0e-6f);
  ASSERT_NE(store.description(firstUUID), nullptr);
  EXPECT_EQ(store.description(firstUUID)->uuid, firstUUID);
  EXPECT_TRUE(store.description(firstUUID)->palette.empty());

  EXPECT_EQ(store.find(secondUUID), nullptr);
  EXPECT_EQ(store.terrains().size(), 1u);
}

TEST(TerrainStore, ReconcileDropsTerrainsWhoseRecordIsGoneAndKeepsTheRest)
{
  AssetRegistry registry;
  registry.registerAsset(terrainRecord(firstUUID));
  registry.registerAsset(terrainRecord(secondUUID));

  terrain::TerrainStore store;
  store.reconcile(registry);

  const auto kept = store.find(firstUUID);
  ASSERT_NE(kept, nullptr);
  kept->setChunk({ 1, 0, 0 }, solidChunk());
  ASSERT_NE(store.find(secondUUID), nullptr);

  registry.removeAsset(secondUUID);
  registry.registerAsset(terrainRecord(thirdUUID));
  store.reconcile(registry);

  EXPECT_EQ(store.find(secondUUID), nullptr);
  EXPECT_NE(store.find(thirdUUID), nullptr);
  EXPECT_EQ(store.find(firstUUID), kept);
  EXPECT_NE(kept->chunk({ 1, 0, 0 }), nullptr);
  EXPECT_EQ(store.terrains().size(), 2u);
}

TEST(TerrainStore, ReconcileDropsATerrainWhoseRecordIsAnotherType)
{
  AssetRegistry registry;
  registry.registerAsset({ .uuid = firstUUID, .type = AssetType::Model, .path = "rock.glb" });

  terrain::TerrainStore store;
  ASSERT_NE(store.create(describe(firstUUID)), nullptr);
  store.reconcile(registry);

  EXPECT_EQ(store.find(firstUUID), nullptr);
}

TEST(TerrainStore, VoxelSizeBoundsAreInclusive)
{
  terrain::TerrainStore store;

  EXPECT_NE(store.create(describe(firstUUID, terrain::minVoxelSize)), nullptr);
  EXPECT_NE(store.create(describe(secondUUID, terrain::maxVoxelSize)), nullptr);

  const float belowMin = std::nextafter(terrain::minVoxelSize, 0.0f);
  const float aboveMax = std::nextafter(terrain::maxVoxelSize, 1.0e9f);
  EXPECT_EQ(store.create(describe(thirdUUID, belowMin)), nullptr);
  EXPECT_EQ(store.create(describe(thirdUUID, aboveMax)), nullptr);
  EXPECT_EQ(store.create(describe(thirdUUID, std::numeric_limits<float>::denorm_min())), nullptr);
  EXPECT_EQ(store.terrains().size(), 2u);
}

TEST(TerrainStore, MaterialNamesMustBeValidUtf8WithoutControlCharacters)
{
  const auto createWithName = [](const std::string& name) {
    terrain::TerrainStore store;
    auto description = describe(firstUUID);
    description.palette.push_back({ name, {}, {} });

    return store.create(description) != nullptr;
  };

  EXPECT_TRUE(createWithName("grass"));
  EXPECT_TRUE(createWithName("caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x8C\xB2"));
  EXPECT_TRUE(createWithName(""));

  EXPECT_FALSE(createWithName("a\xFF" "b"));
  EXPECT_FALSE(createWithName("a\x80" "b"));
  EXPECT_FALSE(createWithName("a\xC3"));
  EXPECT_FALSE(createWithName("a\xE2\x82"));
  EXPECT_FALSE(createWithName("\xC3" "a"));
  EXPECT_FALSE(createWithName("\xC0\xAF"));
  EXPECT_FALSE(createWithName("\xE0\x80\xAF"));
  EXPECT_FALSE(createWithName("\xED\xA0\x80"));
  EXPECT_FALSE(createWithName("\xF4\x90\x80\x80"));
  EXPECT_FALSE(createWithName("a\x01" "b"));
  EXPECT_FALSE(createWithName(std::string("a\0b", 3)));
  EXPECT_FALSE(createWithName("a\x7F" "b"));
  EXPECT_FALSE(createWithName("a\nb"));
}

TEST(TerrainStore, ApplyDescriptionKeepsTheVolumeWhenTheVoxelSizeIsUnchanged)
{
  terrain::TerrainStore store;
  const auto volume = store.create(describe(firstUUID, 4.0f));
  ASSERT_NE(volume, nullptr);
  volume->setChunk({ 0, 0, 0 }, solidChunk());

  auto next = describe(firstUUID, 4.0f);
  next.palette.push_back({ "rock", secondUUID, thirdUUID });
  next.chunkCount = 9;

  EXPECT_TRUE(store.applyDescription(next));
  EXPECT_EQ(store.find(firstUUID), volume);
  EXPECT_NE(volume->chunk({ 0, 0, 0 }), nullptr);

  const auto* stored = store.description(firstUUID);
  ASSERT_NE(stored, nullptr);
  EXPECT_EQ(stored->chunkCount, 9u);
  ASSERT_EQ(stored->palette.size(), 1u);
  EXPECT_EQ(stored->palette[0].name, "rock");
}

TEST(TerrainStore, ApplyDescriptionReplacesTheVolumeWhenTheVoxelSizeChanges)
{
  terrain::TerrainStore store;
  const auto volume = store.create(describe(firstUUID, 4.0f));
  ASSERT_NE(volume, nullptr);
  volume->setChunk({ 0, 0, 0 }, solidChunk());

  EXPECT_TRUE(store.applyDescription(describe(firstUUID, 2.0f)));

  const auto replaced = store.find(firstUUID);
  ASSERT_NE(replaced, nullptr);
  EXPECT_NE(replaced, volume);
  EXPECT_NEAR(replaced->voxelSize(), 2.0f, 1.0e-6f);
  EXPECT_TRUE(replaced->chunks().empty());
  EXPECT_NEAR(store.description(firstUUID)->voxelSize, 2.0f, 1.0e-6f);
}

TEST(TerrainStore, ApplyDescriptionRefusesAnUnknownUuidOrAnInvalidDescription)
{
  terrain::TerrainStore store;
  const auto volume = store.create(describe(firstUUID, 4.0f));
  ASSERT_NE(volume, nullptr);

  EXPECT_FALSE(store.applyDescription(describe(secondUUID)));
  EXPECT_EQ(store.find(secondUUID), nullptr);

  EXPECT_FALSE(store.applyDescription(describe(firstUUID, 0.0f)));
  EXPECT_FALSE(store.applyDescription(describe(firstUUID, std::numeric_limits<float>::quiet_NaN())));
  EXPECT_EQ(store.find(firstUUID), volume);
  EXPECT_NEAR(store.description(firstUUID)->voxelSize, 4.0f, 1.0e-6f);

  EXPECT_TRUE(store.applyDescription(describe(firstUUID, 4.0f)));
}

TEST(TerrainStore, AdoptInstallsTheGivenVolumeAndReplacesAnEntryWithTheSameUuid)
{
  terrain::TerrainStore store;
  const auto original = store.create(describe(firstUUID));
  ASSERT_NE(original, nullptr);

  auto adopted = std::make_shared<terrain::TerrainVolume>(4.0f);
  adopted->setChunk({ 0, 0, 0 }, solidChunk());

  auto description = describe(firstUUID, 4.0f);
  description.palette.push_back({ "rock", {}, {} });

  EXPECT_TRUE(store.adopt(description, adopted));
  EXPECT_EQ(store.find(firstUUID), adopted);
  EXPECT_NE(store.find(firstUUID), original);
  ASSERT_NE(store.description(firstUUID), nullptr);
  EXPECT_EQ(store.description(firstUUID)->voxelSize, 4.0f);
  EXPECT_EQ(store.description(firstUUID)->palette.size(), 1u);
  EXPECT_EQ(store.terrains().size(), 1u);

  EXPECT_TRUE(store.adopt(describe(secondUUID), std::make_shared<terrain::TerrainVolume>(2.5f)));
  EXPECT_EQ(store.terrains().size(), 2u);
}

TEST(TerrainStore, AdoptRefusalsLeaveTheOldEntryAlone)
{
  terrain::TerrainStore store;
  const auto original = store.create(describe(firstUUID));
  ASSERT_NE(original, nullptr);

  EXPECT_FALSE(store.adopt(describe(firstUUID, 0.0f), std::make_shared<terrain::TerrainVolume>(2.5f)));
  EXPECT_FALSE(store.adopt(describe(firstUUID), nullptr));
  EXPECT_FALSE(store.adopt(describe(firstUUID, 2.5f), std::make_shared<terrain::TerrainVolume>(3.0f)));
  EXPECT_FALSE(store.adopt(describe(secondUUID), nullptr));

  EXPECT_EQ(store.find(firstUUID), original);
  EXPECT_EQ(store.description(firstUUID)->voxelSize, 2.5f);
  EXPECT_EQ(store.find(secondUUID), nullptr);

  // Positive control: a matching adopt does take effect.
  const auto good = std::make_shared<terrain::TerrainVolume>(2.5f);
  EXPECT_TRUE(store.adopt(describe(firstUUID), good));
  EXPECT_EQ(store.find(firstUUID), good);
}

TEST(TerrainStore, ResetVolumesGivesEveryTerrainAFreshEmptyVolumeAndKeepsTheDescriptions)
{
  terrain::TerrainStore store;
  auto description = describe(firstUUID, 4.0f);
  description.palette.push_back({ "grass", secondUUID, thirdUUID });
  const auto first = store.create(description);
  const auto second = store.create(describe(secondUUID, 3.0f));
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);

  first->setChunk({ 0, 0, 0 }, terrain::TerrainChunk::uniform(-terrain::airDistance, 1));
  second->setChunk({ 1, 0, 0 }, terrain::TerrainChunk::uniform(-terrain::airDistance, 1));
  ASSERT_FALSE(first->chunks().empty());

  store.resetVolumes();

  const auto resetFirst = store.find(firstUUID);
  const auto resetSecond = store.find(secondUUID);
  ASSERT_NE(resetFirst, nullptr);
  ASSERT_NE(resetSecond, nullptr);
  EXPECT_NE(resetFirst, first);
  EXPECT_NE(resetSecond, second);
  EXPECT_TRUE(resetFirst->chunks().empty());
  EXPECT_TRUE(resetSecond->chunks().empty());
  EXPECT_NEAR(resetFirst->voxelSize(), 4.0f, 1.0e-6f);
  EXPECT_NEAR(resetSecond->voxelSize(), 3.0f, 1.0e-6f);

  // Positive control: the old volumes still hold their chunks, so the reset replaced them rather than clearing them.
  EXPECT_FALSE(first->chunks().empty());

  ASSERT_NE(store.description(firstUUID), nullptr);
  EXPECT_NEAR(store.description(firstUUID)->voxelSize, 4.0f, 1.0e-6f);
  ASSERT_EQ(store.description(firstUUID)->palette.size(), 1u);
  EXPECT_EQ(store.description(firstUUID)->palette[0].name, "grass");
  EXPECT_EQ(store.terrains().size(), 2u);
}

namespace {
  const terrain::ChunkCoord coordA{ 0, 0, 0 };
  const terrain::ChunkCoord coordB{ 1, 0, 0 };
}

TEST(TerrainStoreRevisions, NextRevisionIsMonotonicAndZeroForAnUnknownTerrain)
{
  terrain::TerrainStore store;
  ASSERT_NE(store.create(describe(firstUUID)), nullptr);

  EXPECT_EQ(store.nextRevision(firstUUID), 1u);
  EXPECT_EQ(store.nextRevision(firstUUID), 2u);
  EXPECT_EQ(store.nextRevision(firstUUID), 3u);
  EXPECT_EQ(store.nextRevision(secondUUID), 0u);
}

TEST(TerrainStoreRevisions, SetChunkRecordsTheRevisionAndRefusesBadInput)
{
  terrain::TerrainStore store;
  ASSERT_NE(store.create(describe(firstUUID)), nullptr);

  EXPECT_EQ(store.revision(firstUUID, coordA), 0u);
  ASSERT_TRUE(store.setChunk(firstUUID, coordA, solidChunk(), 4));
  EXPECT_EQ(store.revision(firstUUID, coordA), 4u);
  EXPECT_NE(store.find(firstUUID)->chunk(coordA), nullptr);

  EXPECT_FALSE(store.setChunk(secondUUID, coordA, solidChunk(), 9));
  EXPECT_EQ(store.revision(secondUUID, coordA), 0u);

  const terrain::ChunkCoord beyond{ terrain::maxChunkCoord + 1, 0, 0 };
  EXPECT_FALSE(store.setChunk(firstUUID, beyond, solidChunk(), 9));
  EXPECT_EQ(store.revision(firstUUID, beyond), 0u);
  EXPECT_EQ(store.find(firstUUID)->chunks().size(), 1u);
  EXPECT_EQ(store.nextRevision(firstUUID), 5u);
}

TEST(TerrainStoreRevisions, ErasingAChunkStillRecordsItsRevision)
{
  terrain::TerrainStore store;
  ASSERT_NE(store.create(describe(firstUUID)), nullptr);
  ASSERT_TRUE(store.setChunk(firstUUID, coordA, solidChunk(), 2));

  ASSERT_TRUE(store.setChunk(firstUUID, coordA, nullptr, 5));

  EXPECT_EQ(store.find(firstUUID)->chunk(coordA), nullptr);
  EXPECT_EQ(store.revision(firstUUID, coordA), 5u);
}

TEST(TerrainStoreRevisions, ARevisionAboveTheCounterRaisesIt)
{
  terrain::TerrainStore store;
  ASSERT_NE(store.create(describe(firstUUID)), nullptr);

  ASSERT_TRUE(store.setChunk(firstUUID, coordA, solidChunk(), 40));
  EXPECT_EQ(store.nextRevision(firstUUID), 41u);

  ASSERT_TRUE(store.setChunk(firstUUID, coordB, solidChunk(), 3));
  EXPECT_EQ(store.nextRevision(firstUUID), 42u);
}

TEST(TerrainStoreRevisions, StampAllChunksCoversExactlyTheHeldChunks)
{
  terrain::TerrainStore store;
  ASSERT_NE(store.create(describe(firstUUID)), nullptr);
  const auto volume = store.find(firstUUID);
  volume->setChunk(coordA, solidChunk());
  volume->setChunk(coordB, solidChunk());

  const uint32_t stamp = store.stampAllChunks(firstUUID);

  EXPECT_EQ(stamp, 1u);
  EXPECT_EQ(store.revision(firstUUID, coordA), stamp);
  EXPECT_EQ(store.revision(firstUUID, coordB), stamp);
  EXPECT_EQ(store.revision(firstUUID, { 2, 0, 0 }), 0u);
  EXPECT_GT(store.nextRevision(firstUUID), stamp);
  EXPECT_EQ(store.stampAllChunks(secondUUID), 0u);
}

TEST(TerrainStoreRevisions, ResettingVolumesClearsRevisionsButTheCounterKeepsRising)
{
  terrain::TerrainStore store;
  ASSERT_NE(store.create(describe(firstUUID)), nullptr);
  ASSERT_TRUE(store.setChunk(firstUUID, coordA, solidChunk(), 7));

  store.resetVolumes();

  EXPECT_EQ(store.revision(firstUUID, coordA), 0u);
  EXPECT_EQ(store.nextRevision(firstUUID), 8u);
}

TEST(TerrainStoreRevisions, ANewVoxelSizeClearsRevisionsButAnUnchangedOneKeepsThem)
{
  terrain::TerrainStore store;
  ASSERT_NE(store.create(describe(firstUUID, 2.5f)), nullptr);
  ASSERT_TRUE(store.setChunk(firstUUID, coordA, solidChunk(), 7));

  ASSERT_TRUE(store.applyDescription(describe(firstUUID, 2.5f)));
  EXPECT_EQ(store.revision(firstUUID, coordA), 7u);

  ASSERT_TRUE(store.applyDescription(describe(firstUUID, 4.0f)));
  EXPECT_EQ(store.revision(firstUUID, coordA), 0u);
  EXPECT_EQ(store.nextRevision(firstUUID), 8u);
}

TEST(TerrainStoreRevisions, AdoptingOverAHeldTerrainClearsRevisionsAndKeepsTheCounter)
{
  terrain::TerrainStore store;
  ASSERT_NE(store.create(describe(firstUUID)), nullptr);
  ASSERT_TRUE(store.setChunk(firstUUID, coordA, solidChunk(), 7));

  ASSERT_TRUE(store.adopt(describe(firstUUID), std::make_shared<terrain::TerrainVolume>(2.5f)));

  EXPECT_EQ(store.revision(firstUUID, coordA), 0u);
  EXPECT_EQ(store.nextRevision(firstUUID), 8u);

  terrain::TerrainStore fresh;
  ASSERT_TRUE(fresh.adopt(describe(secondUUID), std::make_shared<terrain::TerrainVolume>(2.5f)));
  EXPECT_EQ(fresh.nextRevision(secondUUID), 1u);
}

TEST(TerrainStoreRevisions, RemoveAndReconcileForgetTheRevisionsAndTheCounter)
{
  terrain::TerrainStore store;
  ASSERT_NE(store.create(describe(firstUUID)), nullptr);
  ASSERT_NE(store.create(describe(secondUUID)), nullptr);
  ASSERT_TRUE(store.setChunk(firstUUID, coordA, solidChunk(), 7));
  ASSERT_TRUE(store.setChunk(secondUUID, coordA, solidChunk(), 7));

  ASSERT_TRUE(store.remove(firstUUID));
  EXPECT_EQ(store.revision(firstUUID, coordA), 0u);
  ASSERT_NE(store.create(describe(firstUUID)), nullptr);
  EXPECT_EQ(store.nextRevision(firstUUID), 1u);

  AssetRegistry empty;
  store.reconcile(empty);
  EXPECT_EQ(store.revision(secondUUID, coordA), 0u);
  EXPECT_EQ(store.nextRevision(secondUUID), 0u);
}

TEST(TerrainStoreRevisions, TheCounterSaturatesInsteadOfWrapping)
{
  terrain::TerrainStore store;
  ASSERT_NE(store.create(describe(firstUUID)), nullptr);
  constexpr uint32_t maximum = std::numeric_limits<uint32_t>::max();

  ASSERT_TRUE(store.setChunk(firstUUID, coordA, solidChunk(), maximum - 1));
  EXPECT_EQ(store.nextRevision(firstUUID), maximum);

  EXPECT_EQ(store.nextRevision(firstUUID), 0u);
  EXPECT_EQ(store.nextRevision(firstUUID), 0u);
  EXPECT_EQ(store.stampAllChunks(firstUUID), 0u);
  EXPECT_EQ(store.revision(firstUUID, coordA), maximum - 1);
}

TEST(TerrainStoreRevisions, SetChunkRefusesRevisionZeroAndRecordsALowerRevisionAsGiven)
{
  terrain::TerrainStore store;
  ASSERT_NE(store.create(describe(firstUUID)), nullptr);
  ASSERT_TRUE(store.setChunk(firstUUID, coordA, solidChunk(), 5));

  EXPECT_FALSE(store.setChunk(firstUUID, coordA, nullptr, 0));
  EXPECT_NE(store.find(firstUUID)->chunk(coordA), nullptr);
  EXPECT_EQ(store.revision(firstUUID, coordA), 5u);

  ASSERT_TRUE(store.setChunk(firstUUID, coordA, solidChunk(), 2));
  EXPECT_EQ(store.revision(firstUUID, coordA), 2u);
}

TEST(TerrainStoreExpectations, ExpectChunksRecordsACountAndZeroClearsIt)
{
  terrain::TerrainStore store;
  ASSERT_NE(store.create(describe(firstUUID)), nullptr);
  EXPECT_FALSE(store.expectedChunks(firstUUID).has_value());

  EXPECT_TRUE(store.expectChunks(firstUUID, 4));
  EXPECT_EQ(store.expectedChunks(firstUUID), std::optional<uint32_t>(4));

  EXPECT_TRUE(store.expectChunks(firstUUID, 0));
  EXPECT_FALSE(store.expectedChunks(firstUUID).has_value());

  EXPECT_TRUE(store.expectChunks(firstUUID, 2));
  store.clearExpectation(firstUUID);
  EXPECT_FALSE(store.expectedChunks(firstUUID).has_value());
}

TEST(TerrainStoreExpectations, AnUnknownTerrainIsRefused)
{
  terrain::TerrainStore store;
  ASSERT_NE(store.create(describe(firstUUID)), nullptr);

  EXPECT_FALSE(store.expectChunks(secondUUID, 3));
  EXPECT_FALSE(store.expectedChunks(secondUUID).has_value());
  store.clearExpectation(secondUUID);

  EXPECT_TRUE(store.expectChunks(firstUUID, 3));
}

TEST(TerrainStoreExpectations, ResetVolumesKeepsTheExpectation)
{
  terrain::TerrainStore store;
  ASSERT_NE(store.create(describe(firstUUID)), nullptr);
  ASSERT_TRUE(store.expectChunks(firstUUID, 6));

  store.resetVolumes();

  EXPECT_EQ(store.expectedChunks(firstUUID), std::optional<uint32_t>(6));
}

TEST(TerrainStoreExpectations, ClearExpectationsForgetsEveryTerrainsExpectation)
{
  terrain::TerrainStore store;
  for (const auto& uuid : { firstUUID, secondUUID })
  {
    ASSERT_NE(store.create(describe(uuid)), nullptr);
    ASSERT_TRUE(store.expectChunks(uuid, 3));
  }

  ASSERT_EQ(store.expectedChunks(secondUUID), std::optional<uint32_t>(3));

  store.clearExpectations();

  EXPECT_FALSE(store.expectedChunks(firstUUID).has_value());
  EXPECT_FALSE(store.expectedChunks(secondUUID).has_value());
  EXPECT_NE(store.find(firstUUID), nullptr);
}

TEST(TerrainStoreExpectations, RemoveReconcileAndAdoptForgetTheExpectation)
{
  AssetRegistry registry;
  registry.registerAsset(terrainRecord(secondUUID));

  terrain::TerrainStore store;
  for (const auto& uuid : { firstUUID, secondUUID, thirdUUID })
  {
    ASSERT_NE(store.create(describe(uuid)), nullptr);
    ASSERT_TRUE(store.expectChunks(uuid, 2));
  }

  ASSERT_TRUE(store.remove(thirdUUID));
  ASSERT_TRUE(store.adopt(describe(firstUUID, 4.0f), std::make_shared<terrain::TerrainVolume>(4.0f)));
  EXPECT_FALSE(store.expectedChunks(firstUUID).has_value());
  EXPECT_EQ(store.expectedChunks(secondUUID), std::optional<uint32_t>(2));

  store.reconcile(registry);

  EXPECT_FALSE(store.expectedChunks(firstUUID).has_value());
  // The terrain the registry keeps is the positive control: reconcile does not clear what it does not drop.
  EXPECT_EQ(store.expectedChunks(secondUUID), std::optional<uint32_t>(2));

  ASSERT_NE(store.create(describe(thirdUUID)), nullptr);
  EXPECT_FALSE(store.expectedChunks(thirdUUID).has_value());

  store.reconcile(AssetRegistry{});
  ASSERT_NE(store.create(describe(secondUUID)), nullptr);
  EXPECT_FALSE(store.expectedChunks(secondUUID).has_value());
}
