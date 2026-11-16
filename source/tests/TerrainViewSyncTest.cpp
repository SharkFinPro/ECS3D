#include <gtest/gtest.h>

#include "ComponentRegistry.h"
#include "WireTypes.h"
#include "ComponentRegistration.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/collisions/Terrain.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"
#include "terrain/TerrainBinding.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainReplication.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainViewSync.h"

#include <Protocol.h>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {
  const auto terrainUUID = uuids::uuid::from_string("11111111-1111-1111-1111-111111111111").value();
  const auto unknownUUID = uuids::uuid::from_string("33333333-3333-3333-3333-333333333333").value();
  const auto sceneUUID = uuids::uuid::from_string("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa").value();
  const auto loadA = uuids::uuid::from_string("44444444-4444-4444-4444-444444444444").value();
  const auto loadB = uuids::uuid::from_string("55555555-5555-5555-5555-555555555555").value();

  std::shared_ptr<const terrain::TerrainChunk> solid(const uint8_t material = 1)
  {
    return terrain::TerrainChunk::uniform(-terrain::airDistance, material);
  }

  net::Message chunksMessage(const uuids::uuid& terrain, const std::vector<terrain::ChunkCoord>& coords,
                             const uint32_t revision = 1)
  {
    std::vector<terrain::ChunkEntry> entries;
    for (const auto& coord : coords)
    {
      entries.push_back(terrain::makeChunkEntry(coord, revision, solid()));
    }

    return terrain::packTerrainChunks(terrain, entries);
  }

  struct View {
    terrain::TerrainStore store;
    terrain::TerrainViewSync sync;
    std::shared_ptr<ComponentRegistry> registry = std::make_shared<ComponentRegistry>();
    SceneManager scenes;
    std::shared_ptr<Terrain> bound;

    explicit View(const uint32_t chunkCount = 0)
    {
      registerDataComponents(*registry);

      store.create({ .uuid = terrainUUID, .chunkCount = chunkCount });

      const auto scene = std::make_shared<SceneAsset>(sceneUUID, "Main", registry);
      scenes.addScene(scene);
      scenes.loadScene(scene);

      const auto object = std::make_shared<Object>("Terrain");
      bound = std::make_shared<Terrain>();
      bound->setTerrainUUID(terrainUUID);
      object->addComponent(bound);
      scene->getObjectManager()->addObject(object);

      terrain::bindTerrainVolumes(store, scenes);
    }

    [[nodiscard]] size_t held() const
    {
      return store.find(terrainUUID)->chunks().size();
    }
  };

  bool hasLine(const terrain::ViewApplyOutcome& outcome, const LogLevel level, const std::string& needle)
  {
    return std::ranges::any_of(outcome.log, [&](const terrain::ViewLogLine& line)
    {
      return line.level == level && line.text.find(needle) != std::string::npos;
    });
  }
}

TEST(TerrainViewSyncTest, AWellFormedMessageIsAppliedAndInvalidatesTheBoundBounds)
{
  View view;
  const auto before = view.bound->getGeometryRevision();

  const auto outcome = view.sync.apply(view.store, view.scenes, chunksMessage(terrainUUID, { { 0, 0, 0 }, { 1, 0, 0 } }));

  EXPECT_FALSE(outcome.malformed);
  EXPECT_EQ(outcome.terrain, terrainUUID);
  EXPECT_EQ(outcome.result.applied, 2u);
  EXPECT_EQ(view.held(), 2u);
  EXPECT_GT(view.bound->getGeometryRevision(), before);
  EXPECT_TRUE(outcome.log.empty());
}

TEST(TerrainViewSyncTest, AMessageThatAppliesNothingLeavesTheBoundsAlone)
{
  View view;
  ASSERT_EQ(view.sync.apply(view.store, view.scenes, chunksMessage(terrainUUID, { { 0, 0, 0 } }, 5)).result.applied, 1u);
  const auto before = view.bound->getGeometryRevision();

  // The same revision again is stale.
  const auto outcome = view.sync.apply(view.store, view.scenes, chunksMessage(terrainUUID, { { 0, 0, 0 } }, 5));

  EXPECT_EQ(outcome.result.applied, 0u);
  EXPECT_EQ(outcome.result.stale, 1u);
  EXPECT_EQ(view.bound->getGeometryRevision(), before);
}

TEST(TerrainViewSyncTest, AMalformedMessageIsRefusedWithAWarningAndTheStoreIsUntouched)
{
  View view;
  const auto before = view.bound->getGeometryRevision();

  net::Message bad(net::MessageType::terrainChunks);
  bad.write(terrainUUID);
  bad.write(static_cast<uint32_t>(3));

  const auto outcome = view.sync.apply(view.store, view.scenes, bad);

  EXPECT_TRUE(outcome.malformed);
  EXPECT_TRUE(hasLine(outcome, LogLevel::warn, "Refused a malformed terrainChunks message (" +
                                                 std::to_string(bad.size()) + " bytes)"));
  EXPECT_EQ(view.held(), 0u);
  EXPECT_EQ(view.bound->getGeometryRevision(), before);

  // Positive control: the well-formed message is accepted with no refusal line.
  const auto good = view.sync.apply(view.store, view.scenes, chunksMessage(terrainUUID, { { 0, 0, 0 } }));
  EXPECT_FALSE(good.malformed);
  EXPECT_FALSE(hasLine(good, LogLevel::warn, "Refused"));
  EXPECT_EQ(view.held(), 1u);
}

TEST(TerrainViewSyncTest, AnUnknownTerrainIsWarnedAboutAndNothingIsApplied)
{
  View view;

  const auto outcome = view.sync.apply(view.store, view.scenes, chunksMessage(unknownUUID, { { 0, 0, 0 } }));

  EXPECT_FALSE(outcome.malformed);
  EXPECT_TRUE(outcome.result.unknownTerrain);
  EXPECT_TRUE(hasLine(outcome, LogLevel::warn, "unknown terrain " + uuids::to_string(unknownUUID)));
  EXPECT_EQ(view.held(), 0u);
}

TEST(TerrainViewSyncTest, AHashOnlyEntryTheStoreCannotSatisfyIsWarnedAbout)
{
  View view;

  const std::vector<terrain::ChunkEntry> entries{ terrain::makeHashOnlyEntry({ 0, 0, 0 }, 1, solid()->hash()) };
  const auto outcome = view.sync.apply(view.store, view.scenes, terrain::packTerrainChunks(terrainUUID, entries));

  EXPECT_EQ(outcome.result.missing.size(), 1u);
  EXPECT_TRUE(hasLine(outcome, LogLevel::warn, "hash-only"));

  // Positive control: a full entry produces no such warning.
  const auto full = view.sync.apply(view.store, view.scenes, chunksMessage(terrainUUID, { { 0, 0, 0 } }));
  EXPECT_FALSE(hasLine(full, LogLevel::warn, "hash-only"));
}

TEST(TerrainViewSyncTest, CompletionIsLoggedOnceWhenTheHeldCountReachesTheDescriptionsCount)
{
  View view(3);

  const auto first = view.sync.apply(view.store, view.scenes, chunksMessage(terrainUUID, { { 0, 0, 0 }, { 1, 0, 0 } }));
  EXPECT_FALSE(hasLine(first, LogLevel::info, "complete"));

  const auto second = view.sync.apply(view.store, view.scenes, chunksMessage(terrainUUID, { { 2, 0, 0 } }));
  EXPECT_TRUE(hasLine(second, LogLevel::info, "Terrain " + uuids::to_string(terrainUUID) + " complete: 3 chunk(s)"));

  // A later message for the same terrain does not report it again.
  const auto third = view.sync.apply(view.store, view.scenes, chunksMessage(terrainUUID, { { 2, 0, 0 } }, 2));
  EXPECT_FALSE(hasLine(third, LogLevel::info, "complete"));
}

TEST(TerrainViewSyncTest, ATerrainWithNoChunksToStreamIsNeverReportedComplete)
{
  View view(0);

  const auto outcome = view.sync.apply(view.store, view.scenes, chunksMessage(terrainUUID, { { 0, 0, 0 } }));
  EXPECT_FALSE(hasLine(outcome, LogLevel::info, "complete"));
}

TEST(TerrainViewSyncTest, TheFirstSnapshotIsNotAProjectChange)
{
  View view;
  ASSERT_EQ(view.sync.apply(view.store, view.scenes, chunksMessage(terrainUUID, { { 0, 0, 0 } })).result.applied, 1u);

  EXPECT_FALSE(view.sync.observeSnapshot(view.store, loadA, 100));
  EXPECT_EQ(view.held(), 1u);
}

TEST(TerrainViewSyncTest, ASnapshotOfTheSameProjectKeepsTheChunks)
{
  View view;
  ASSERT_FALSE(view.sync.observeSnapshot(view.store, loadA, 100));
  ASSERT_EQ(view.sync.apply(view.store, view.scenes, chunksMessage(terrainUUID, { { 0, 0, 0 } })).result.applied, 1u);

  EXPECT_FALSE(view.sync.observeSnapshot(view.store, loadA, 100));
  EXPECT_EQ(view.held(), 1u);
}

TEST(TerrainViewSyncTest, ADifferentLoadIdResetsTheVolumesAndTheirRevisions)
{
  View view;
  ASSERT_FALSE(view.sync.observeSnapshot(view.store, loadA, 100));
  ASSERT_EQ(view.sync.apply(view.store, view.scenes, chunksMessage(terrainUUID, { { 0, 0, 0 } }, 9)).result.applied, 1u);

  EXPECT_TRUE(view.sync.observeSnapshot(view.store, loadB, 100));
  EXPECT_EQ(view.held(), 0u);
  EXPECT_EQ(view.store.revision(terrainUUID, { 0, 0, 0 }), 0u);

  // The new project's lower revision now applies.
  EXPECT_EQ(view.sync.apply(view.store, view.scenes, chunksMessage(terrainUUID, { { 0, 0, 0 } }, 1)).result.applied, 1u);
}

TEST(TerrainViewSyncTest, ADifferentFingerprintResetsTheVolumes)
{
  View view;
  ASSERT_FALSE(view.sync.observeSnapshot(view.store, loadA, 100));
  ASSERT_EQ(view.sync.apply(view.store, view.scenes, chunksMessage(terrainUUID, { { 0, 0, 0 } })).result.applied, 1u);

  EXPECT_TRUE(view.sync.observeSnapshot(view.store, loadA, 0));
  EXPECT_EQ(view.held(), 0u);
}

TEST(TerrainViewSyncTest, AProjectChangeLetsTheNextProjectReportCompletionAgain)
{
  View view(1);
  ASSERT_FALSE(view.sync.observeSnapshot(view.store, loadA, 100));
  ASSERT_TRUE(hasLine(view.sync.apply(view.store, view.scenes, chunksMessage(terrainUUID, { { 0, 0, 0 } })),
                      LogLevel::info, "complete"));

  ASSERT_TRUE(view.sync.observeSnapshot(view.store, loadB, 100));

  const auto outcome = view.sync.apply(view.store, view.scenes, chunksMessage(terrainUUID, { { 0, 0, 0 } }));
  EXPECT_TRUE(hasLine(outcome, LogLevel::info, "complete"));
}
