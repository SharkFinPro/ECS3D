#include <gtest/gtest.h>

#include "terrain/TerrainPlaySnapshot.h"
#include "terrain/TerrainReplication.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainStreamer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <cstdint>
#include <memory>
#include <tuple>
#include <vector>

#include "TerrainStreamerFixtures.h"

using namespace terrainStreamerFixtures;

namespace {
  using Coords = std::vector<ChunkCoord>;

  std::map<std::tuple<int, int, int>, uint64_t> hashes(const terrain::TerrainStore& store, const uuids::uuid& uuid)
  {
    std::map<std::tuple<int, int, int>, uint64_t> found;
    for (const auto& [coord, chunk] : store.find(uuid)->chunks())
    {
      found[{ coord.x, coord.y, coord.z }] = chunk->hash();
    }

    return found;
  }

  // Mirrors what play could do: change { 0 }, add { 9 }, erase { 1 }; { 2 } stays untouched.
  void playOn(terrain::TerrainStore& store, const uuids::uuid& uuid)
  {
    ASSERT_TRUE(store.setChunk(uuid, { 0, 0, 0 }, noise(900), store.nextRevision(uuid)));
    ASSERT_TRUE(store.setChunk(uuid, { 9, 0, 0 }, noise(901), store.nextRevision(uuid)));
    ASSERT_TRUE(store.setChunk(uuid, { 1, 0, 0 }, nullptr, store.nextRevision(uuid)));
  }
}

TEST(TerrainPlaySnapshotTest, RestoreReturnsExactlyTheChangedAddedAndErasedCoordinates)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainA, 4);
  const auto before = store.find(terrainA)->snapshotChunks();
  const auto untouchedRevision = store.revision(terrainA, { 2, 0, 0 });

  terrain::TerrainPlaySnapshot snapshot;
  snapshot.capture(store);
  ASSERT_FALSE(snapshot.empty());
  playOn(store, terrainA);

  const uint32_t changedRevision = store.revision(terrainA, { 0, 0, 0 });
  const uint32_t addedRevision = store.revision(terrainA, { 9, 0, 0 });
  const uint32_t erasedRevision = store.revision(terrainA, { 1, 0, 0 });

  const auto restored = snapshot.restore(store).terrains;
  ASSERT_EQ(restored.size(), 1u);
  EXPECT_EQ(restored[0].first, terrainA);
  EXPECT_EQ(restored[0].second, (Coords{ { 0, 0, 0 }, { 1, 0, 0 }, { 9, 0, 0 } }));

  const auto after = store.find(terrainA)->snapshotChunks();
  EXPECT_EQ(after, before);
  EXPECT_EQ(store.find(terrainA)->chunk({ 9, 0, 0 }), nullptr);

  EXPECT_GT(store.revision(terrainA, { 0, 0, 0 }), changedRevision);
  EXPECT_GT(store.revision(terrainA, { 9, 0, 0 }), addedRevision);
  EXPECT_GT(store.revision(terrainA, { 1, 0, 0 }), erasedRevision);
  EXPECT_EQ(store.revision(terrainA, { 2, 0, 0 }), untouchedRevision);

  EXPECT_TRUE(snapshot.empty());
  EXPECT_TRUE(snapshot.restore(store).terrains.empty());
}

TEST(TerrainPlaySnapshotTest, RestoreWithNothingChangedReturnsNothing)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainA, 3);

  terrain::TerrainPlaySnapshot snapshot;
  snapshot.capture(store);
  const auto revision = store.revision(terrainA, { 0, 0, 0 });

  EXPECT_TRUE(snapshot.restore(store).terrains.empty());
  EXPECT_EQ(store.revision(terrainA, { 0, 0, 0 }), revision);

  snapshot.capture(store);
  ASSERT_TRUE(store.setChunk(terrainA, { 0, 0, 0 }, noise(900), store.nextRevision(terrainA)));
  EXPECT_FALSE(snapshot.restore(store).terrains.empty());
}

TEST(TerrainPlaySnapshotTest, ATerrainCreatedAfterCaptureIsLeftAlone)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainA, 2);

  terrain::TerrainPlaySnapshot snapshot;
  snapshot.capture(store);
  addTerrain(store, terrainB, 2);
  const auto bBefore = store.find(terrainB)->snapshotChunks();
  ASSERT_TRUE(store.setChunk(terrainA, { 0, 0, 0 }, noise(900), store.nextRevision(terrainA)));
  ASSERT_TRUE(store.setChunk(terrainB, { 0, 0, 0 }, noise(900), store.nextRevision(terrainB)));

  const auto restored = snapshot.restore(store).terrains;
  ASSERT_EQ(restored.size(), 1u);
  EXPECT_EQ(restored[0].first, terrainA);
  EXPECT_NE(store.find(terrainB)->chunk({ 0, 0, 0 }), bBefore.at({ 0, 0, 0 }));
}

TEST(TerrainPlaySnapshotTest, ARemovedTerrainIsSkippedAndTheOthersStillRestore)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainA, 2);
  addTerrain(store, terrainB, 2);

  terrain::TerrainPlaySnapshot snapshot;
  snapshot.capture(store);
  ASSERT_TRUE(store.setChunk(terrainB, { 0, 0, 0 }, noise(900), store.nextRevision(terrainB)));
  ASSERT_TRUE(store.remove(terrainA));

  const auto restored = snapshot.restore(store).terrains;
  ASSERT_EQ(restored.size(), 1u);
  EXPECT_EQ(restored[0].first, terrainB);
}

TEST(TerrainPlaySnapshotTest, ATerrainWhoseVoxelSizeChangedIsSkipped)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainA, 2);
  addTerrain(store, terrainB, 2);

  terrain::TerrainPlaySnapshot snapshot;
  snapshot.capture(store);

  auto description = *store.description(terrainA);
  description.voxelSize = 5.0f;
  ASSERT_TRUE(store.applyDescription(description));
  ASSERT_TRUE(store.setChunk(terrainA, { 0, 0, 0 }, noise(900), store.nextRevision(terrainA)));
  ASSERT_TRUE(store.setChunk(terrainB, { 0, 0, 0 }, noise(900), store.nextRevision(terrainB)));

  const auto restored = snapshot.restore(store).terrains;
  ASSERT_EQ(restored.size(), 1u);
  EXPECT_EQ(restored[0].first, terrainB);
  EXPECT_EQ(store.find(terrainA)->chunks().size(), 1u);
}

TEST(TerrainPlaySnapshotTest, ACoordinateTheExhaustedCounterRefusesIsReportedNotReturned)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainA, 3);
  addTerrain(store, terrainB, 3);

  terrain::TerrainPlaySnapshot snapshot;
  snapshot.capture(store);
  ASSERT_TRUE(store.setChunk(terrainA, { 0, 0, 0 }, noise(900), store.nextRevision(terrainA)));
  ASSERT_TRUE(store.setChunk(terrainA, { 1, 0, 0 }, noise(901), UINT32_MAX));
  ASSERT_EQ(store.nextRevision(terrainA), 0u);
  ASSERT_TRUE(store.setChunk(terrainB, { 0, 0, 0 }, noise(900), store.nextRevision(terrainB)));

  const auto result = snapshot.restore(store);
  EXPECT_EQ(result.failed, 2u);
  ASSERT_EQ(result.terrains.size(), 1u);
  EXPECT_EQ(result.terrains[0].first, terrainB);
  EXPECT_EQ(result.terrains[0].second, (Coords{ { 0, 0, 0 } }));
  EXPECT_NE(store.find(terrainA)->chunk({ 0, 0, 0 }), nullptr);
}

TEST(TerrainPlaySnapshotTest, StopRestoresAViewThatWasStreamedTheChangedChunks)
{
  terrain::TerrainStore server;
  addTerrain(server, terrainA, 4);
  terrain::TerrainStore view;
  ASSERT_NE(view.create({ .uuid = terrainA }), nullptr);

  terrain::TerrainStreamer streamer;
  Recorder recorder;
  const auto apply = [&]()
  {
    for (const auto& received : recorder.messages)
    {
      terrain::applyTerrainChunks(view, received.payload);
    }
    recorder.messages.clear();
  };

  streamer.start(1, server, originFocus);
  pumpUntilDone(streamer, server, recorder);
  apply();
  const auto captured = hashes(server, terrainA);
  ASSERT_EQ(hashes(view, terrainA), captured);

  terrain::TerrainPlaySnapshot snapshot;
  snapshot.capture(server);
  playOn(server, terrainA);
  const Coords played{ { 0, 0, 0 }, { 1, 0, 0 }, { 9, 0, 0 } };
  streamer.requeue(terrainA, played);
  pumpUntilDone(streamer, server, recorder);
  apply();
  ASSERT_EQ(hashes(view, terrainA), hashes(server, terrainA));
  ASSERT_NE(hashes(view, terrainA), captured);

  const auto restoredTerrains = snapshot.restore(server);
  for (const auto& [uuid, coords] : restoredTerrains.terrains)
  {
    streamer.requeue(uuid, coords);
  }
  pumpUntilDone(streamer, server, recorder);
  apply();

  EXPECT_EQ(hashes(server, terrainA), captured);
  EXPECT_EQ(hashes(view, terrainA), captured);
}
