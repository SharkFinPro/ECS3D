#include <gtest/gtest.h>

#include "terrain/TerrainChunk.h"
#include "terrain/TerrainCodec.h"
#include "terrain/TerrainReplication.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainStreamer.h"
#include "terrain/TerrainTypes.h"

#include <Protocol.h>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <unordered_map>
#include <memory>
#include <stdexcept>
#include <random>
#include <utility>
#include <vector>

#include "TerrainStreamerFixtures.h"

using namespace terrainStreamerFixtures;

TEST(TerrainStreamerTest, AJoinerReceivesEveryChunkOnceWithTheServersRevisionsAndHashes)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainA, 7);

  terrain::TerrainStreamer streamer;
  streamer.start(1, store, originFocus);
  ASSERT_TRUE(streamer.streaming(1));
  EXPECT_EQ(streamer.pendingChunks(1), 7u);

  Recorder recorder;
  pumpUntilDone(streamer, store, recorder);

  EXPECT_FALSE(streamer.streaming(1));
  EXPECT_EQ(streamer.pendingChunks(1), 0u);
  ASSERT_EQ(chunkTotal(recorder.messages), 7u);

  std::unordered_map<ChunkCoord, int> seen;
  for (const Received& received : recorder.messages)
  {
    EXPECT_EQ(received.payload.terrain, terrainA);

    for (const auto& entry : received.payload.entries)
    {
      ++seen[entry.coord];
      EXPECT_GT(entry.revision, 0u);
      EXPECT_EQ(entry.revision, store.revision(terrainA, entry.coord));
      ASSERT_NE(entry.chunk, nullptr);
      EXPECT_EQ(entry.hash, store.find(terrainA)->chunk(entry.coord)->hash());
    }
  }

  EXPECT_EQ(seen.size(), 7u);
  EXPECT_TRUE(std::ranges::all_of(seen, [](const auto& entry) { return entry.second == 1; }));
}

TEST(TerrainStreamerTest, EveryPumpStaysWithinTheBudgetAndTheBudgetDoesSplitTheStream)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainA, 6);

  terrain::TerrainStreamer streamer;
  streamer.start(1, store, originFocus);

  const size_t budget = terrain::defaultStreamBudgetBytes;
  Recorder recorder;
  size_t pumps = 0;

  while (streamer.streaming(1))
  {
    const size_t before = recorder.messages.size();
    streamer.pump(store, recorder.sender(), budget);

    size_t bytes = 0;
    for (size_t i = before; i < recorder.messages.size(); ++i)
    {
      bytes += recorder.messages[i].bytes;
    }

    EXPECT_LE(bytes, budget);
    ++pumps;
    ASSERT_LT(pumps, 100u);
  }

  EXPECT_EQ(chunkTotal(recorder.messages), 6u);
  EXPECT_GT(pumps, 1u);
}

TEST(TerrainStreamerTest, ABudgetSmallerThanOneChunkStillSendsOneChunkPerPump)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainA, 3);

  terrain::TerrainStreamer streamer;
  streamer.start(1, store, originFocus);

  Recorder recorder;
  streamer.pump(store, recorder.sender(), 1);

  ASSERT_EQ(recorder.messages.size(), 1u);
  EXPECT_EQ(recorder.messages.front().payload.entries.size(), 1u);
  EXPECT_GT(recorder.messages.front().bytes, 1u);
  EXPECT_EQ(streamer.pendingChunks(1), 2u);

  // Positive control: a generous budget finishes the rest in one pump.
  streamer.pump(store, recorder.sender());
  EXPECT_EQ(chunkTotal(recorder.messages), 3u);
  EXPECT_FALSE(streamer.streaming(1));
}

TEST(TerrainStreamerTest, NoMessageCarriesMoreThanTheEntryCap)
{
  constexpr int count = static_cast<int>(terrain::maxTerrainChunksEntries) + 76;

  terrain::TerrainStore store;
  addTerrain(store, terrainA, count, false);

  terrain::TerrainStreamer streamer;
  streamer.start(1, store, originFocus);

  Recorder recorder;
  pumpUntilDone(streamer, store, recorder);

  EXPECT_EQ(chunkTotal(recorder.messages), static_cast<size_t>(count));
  for (const Received& received : recorder.messages)
  {
    EXPECT_LE(received.payload.entries.size(), terrain::maxTerrainChunksEntries);
  }

  // Positive control: the cap was reached, so the stream really did need a split.
  ASSERT_GE(recorder.messages.size(), 2u);
  EXPECT_EQ(recorder.messages.front().payload.entries.size(), terrain::maxTerrainChunksEntries);
}

TEST(TerrainStreamerTest, TwoConnectionsStreamIndependently)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainA, 4);

  terrain::TerrainStreamer streamer;
  streamer.start(1, store, originFocus);

  Recorder recorder;
  streamer.pump(store, recorder.sender(), 1);
  ASSERT_EQ(streamer.pendingChunks(1), 3u);

  streamer.start(2, store, originFocus);
  EXPECT_EQ(streamer.pendingChunks(2), 4u);
  EXPECT_EQ(streamer.pendingChunks(1), 3u);

  for (int i = 0; i < 10000 && (streamer.streaming(1) || streamer.streaming(2)); ++i)
  {
    streamer.pump(store, recorder.sender(), 1);
  }

  EXPECT_FALSE(streamer.streaming(1));
  EXPECT_FALSE(streamer.streaming(2));

  EXPECT_EQ(chunkTotal(recorder.from(1)), 4u);
  EXPECT_EQ(chunkTotal(recorder.from(2)), 4u);
}

TEST(TerrainStreamerTest, ForgetStopsOneConnectionAndLeavesTheOther)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainA, 4);

  terrain::TerrainStreamer streamer;
  streamer.start(1, store, originFocus);
  streamer.start(2, store, originFocus);

  Recorder recorder;
  streamer.pump(store, recorder.sender(), 1);
  ASSERT_EQ(recorder.from(1).size(), 1u);
  ASSERT_EQ(recorder.from(2).size(), 1u);

  streamer.forget(1);
  EXPECT_FALSE(streamer.streaming(1));
  EXPECT_EQ(streamer.pendingChunks(1), 0u);

  streamer.pump(store, recorder.sender(), 1);
  EXPECT_EQ(recorder.from(1).size(), 1u);
  EXPECT_EQ(recorder.from(2).size(), 2u);
  EXPECT_TRUE(streamer.streaming(2));
}

TEST(TerrainStreamerTest, StartingAgainReplacesTheStreamFromTheCurrentVolume)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainA, 3);

  terrain::TerrainStreamer streamer;
  streamer.start(1, store, originFocus);

  Recorder recorder;
  streamer.pump(store, recorder.sender(), 1);
  ASSERT_EQ(streamer.pendingChunks(1), 2u);

  streamer.start(1, store, originFocus);
  EXPECT_EQ(streamer.pendingChunks(1), 3u);
}

TEST(TerrainStreamerTest, RequeueSendsAnEditedChunkAgainWithAHigherRevision)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainA, 2);

  terrain::TerrainStreamer streamer;
  streamer.start(1, store, originFocus);

  Recorder recorder;
  pumpUntilDone(streamer, store, recorder);
  const uint32_t before = store.revision(terrainA, { 0, 0, 0 });
  const size_t sent = chunkTotal(recorder.messages);

  // With nothing requeued a pump sends nothing.
  streamer.pump(store, recorder.sender());
  EXPECT_EQ(chunkTotal(recorder.messages), sent);

  const auto edited = solid(9);
  ASSERT_TRUE(store.setChunk(terrainA, { 0, 0, 0 }, edited, store.nextRevision(terrainA)));

  const std::vector<ChunkCoord> coords{ { 0, 0, 0 } };
  streamer.requeue(terrainA, coords);
  EXPECT_TRUE(streamer.streaming(1));

  streamer.pump(store, recorder.sender());

  ASSERT_EQ(chunkTotal(recorder.messages), sent + 1);
  const auto& entry = recorder.messages.back().payload.entries.front();
  EXPECT_EQ(entry.coord, (ChunkCoord{ 0, 0, 0 }));
  EXPECT_GT(entry.revision, before);
  EXPECT_EQ(entry.hash, edited->hash());
  EXPECT_FALSE(streamer.streaming(1));
}

TEST(TerrainStreamerTest, RequeueForAnotherTerrainChangesNothing)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainA, 1);

  terrain::TerrainStreamer streamer;
  streamer.start(1, store, originFocus);

  Recorder recorder;
  pumpUntilDone(streamer, store, recorder);

  const std::vector<ChunkCoord> coords{ { 0, 0, 0 } };
  streamer.requeue(terrainB, coords);
  EXPECT_FALSE(streamer.streaming(1));

  streamer.requeue(terrainA, coords);
  EXPECT_TRUE(streamer.streaming(1));
}
