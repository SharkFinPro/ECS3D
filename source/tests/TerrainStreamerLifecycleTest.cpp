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

TEST(TerrainStreamerTest, RestartAfterResetVolumesEndsTheStream)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainA, 4);

  terrain::TerrainStreamer streamer;
  streamer.start(1, store, originFocus);

  Recorder recorder;
  streamer.pump(store, recorder.sender(), 1);
  ASSERT_TRUE(streamer.streaming(1));
  ASSERT_GT(streamer.pendingChunks(1), 0u);

  store.resetVolumes();
  streamer.restart(terrainA, store, originFocus);

  EXPECT_FALSE(streamer.streaming(1));
  EXPECT_EQ(streamer.pendingChunks(1), 0u);

  const size_t sent = chunkTotal(recorder.messages);
  streamer.pump(store, recorder.sender());
  EXPECT_EQ(chunkTotal(recorder.messages), sent);

  // The restarted stream still exists: a chunk added now and requeued is streamed.
  ASSERT_TRUE(store.setChunk(terrainA, { 0, 0, 0 }, solid(3), store.nextRevision(terrainA)));
  const std::vector<ChunkCoord> coords{ { 0, 0, 0 } };
  streamer.requeue(terrainA, coords);
  EXPECT_TRUE(streamer.streaming(1));

  streamer.pump(store, recorder.sender());
  ASSERT_EQ(chunkTotal(recorder.messages), sent + 1);
  EXPECT_EQ(recorder.messages.back().payload.entries.front().coord, (ChunkCoord{ 0, 0, 0 }));
}

TEST(TerrainStreamerTest, RestartStreamsAReplacedVolumeFromItsCurrentChunks)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainA, 4);

  terrain::TerrainStreamer streamer;
  streamer.start(1, store, originFocus);

  const auto description = *store.description(terrainA);
  const auto replacement = std::make_shared<terrain::TerrainVolume>(description.voxelSize);
  replacement->setChunk({ 40, 0, 0 }, solid());
  ASSERT_TRUE(store.adopt(description, replacement));
  streamer.restart(terrainA, store, originFocus);

  EXPECT_EQ(streamer.pendingChunks(1), 1u);

  Recorder recorder;
  pumpUntilDone(streamer, store, recorder);

  ASSERT_EQ(chunkTotal(recorder.messages), 1u);
  EXPECT_EQ(recorder.messages.front().payload.entries.front().coord, (ChunkCoord{ 40, 0, 0 }));
}

TEST(TerrainStreamerTest, AnUnstampedChunkIsStampedBeforeItIsSentAndAStampedOneKeepsItsRevision)
{
  terrain::TerrainStore store;
  const auto volume = store.create({ .uuid = terrainA });
  ASSERT_NE(volume, nullptr);

  const auto stamped = solid(2);
  volume->setChunk({ 0, 0, 0 }, solid(1));
  volume->setChunk({ 1, 0, 0 }, stamped);
  ASSERT_TRUE(store.setChunk(terrainA, { 1, 0, 0 }, stamped, 7));
  ASSERT_EQ(store.revision(terrainA, { 0, 0, 0 }), 0u);

  terrain::TerrainStreamer streamer;
  streamer.start(1, store, originFocus);

  Recorder recorder;
  pumpUntilDone(streamer, store, recorder);

  std::unordered_map<ChunkCoord, uint32_t> revisions;
  for (const Received& received : recorder.messages)
  {
    for (const auto& entry : received.payload.entries)
    {
      revisions[entry.coord] = entry.revision;
    }
  }

  ASSERT_EQ(revisions.size(), 2u);
  const ChunkCoord unstamped{ 0, 0, 0 };
  const ChunkCoord keptStamp{ 1, 0, 0 };
  EXPECT_GT(revisions[unstamped], 0u);
  EXPECT_EQ(store.revision(terrainA, unstamped), revisions[unstamped]);
  EXPECT_EQ(revisions[keptStamp], 7u);
}

TEST(TerrainStreamerTest, ATerrainTheStoreNoLongerHoldsIsDroppedSilently)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainA, 3);
  addTerrain(store, terrainB, 2, false);

  terrain::TerrainStreamer streamer;
  streamer.start(1, store, originFocus);
  ASSERT_EQ(streamer.pendingChunks(1), 5u);

  ASSERT_TRUE(store.remove(terrainA));

  Recorder recorder;
  pumpUntilDone(streamer, store, recorder);

  EXPECT_EQ(chunkTotal(recorder.messages), 2u);
  EXPECT_TRUE(std::ranges::all_of(recorder.messages,
                                  [](const Received& received) { return received.payload.terrain == terrainB; }));
}

TEST(TerrainStreamerTest, TerrainsAreServedInUUIDOrderFromOneBudget)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainB, 2);
  addTerrain(store, terrainA, 2);

  terrain::TerrainStreamer streamer;
  streamer.start(1, store, originFocus);

  Recorder recorder;
  pumpUntilDone(streamer, store, recorder, 1);

  ASSERT_EQ(recorder.messages.size(), 4u);
  EXPECT_EQ(recorder.messages[0].payload.terrain, terrainA);
  EXPECT_EQ(recorder.messages[1].payload.terrain, terrainA);
  EXPECT_EQ(recorder.messages[2].payload.terrain, terrainB);
  EXPECT_EQ(recorder.messages[3].payload.terrain, terrainB);
}

TEST(TerrainStreamerTest, AChunkThatDoesNotFitTheRestOfTheBudgetWaitsForTheNextPump)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainA, 1, false);
  addTerrain(store, terrainB, 1);

  terrain::TerrainStreamer streamer;
  streamer.start(1, store, originFocus);

  // Room for the small chunk but not for the dense one after it.
  const size_t budget = terrain::streamMessageOverheadBytes + terrain::streamEntryOverheadBytes +
                        terrain::encodeChunk(*solid()).size() + 100;
  Recorder recorder;
  streamer.pump(store, recorder.sender(), budget);

  ASSERT_EQ(recorder.messages.size(), 1u);
  EXPECT_EQ(recorder.messages.front().payload.terrain, terrainA);
  EXPECT_EQ(streamer.pendingChunks(1), 1u);

  // The deferred chunk is sent whole on the next pump, not lost.
  streamer.pump(store, recorder.sender(), budget);
  ASSERT_EQ(recorder.messages.size(), 2u);
  EXPECT_EQ(recorder.messages.back().payload.terrain, terrainB);
  EXPECT_EQ(recorder.messages.back().payload.entries.size(), 1u);
}

TEST(TerrainStreamerTest, PumpReportsAFinishedStreamOnceWithItsChunkCount)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainA, 3);

  terrain::TerrainStreamer streamer;
  streamer.start(1, store, originFocus);

  Recorder recorder;
  EXPECT_TRUE(streamer.pump(store, recorder.sender(), 1).empty());

  std::vector<terrain::StreamFinished> finished;
  while (streamer.streaming(1))
  {
    const auto round = streamer.pump(store, recorder.sender(), 1);
    finished.insert(finished.end(), round.begin(), round.end());
  }

  ASSERT_EQ(finished.size(), 1u);
  EXPECT_EQ(finished.front().connId, 1);
  EXPECT_EQ(finished.front().chunks, 3u);

  EXPECT_TRUE(streamer.pump(store, recorder.sender()).empty());
}

TEST(TerrainStreamerTest, StartingWithNoChunksIsNotStreaming)
{
  terrain::TerrainStore store;
  ASSERT_NE(store.create({ .uuid = terrainA }), nullptr);

  terrain::TerrainStreamer streamer;
  streamer.start(1, store, originFocus);
  EXPECT_FALSE(streamer.streaming(1));

  Recorder recorder;
  EXPECT_TRUE(streamer.pump(store, recorder.sender()).empty());
  EXPECT_TRUE(recorder.messages.empty());
}

TEST(TerrainStreamerTest, EachConnectionStartsFromItsOwnFocus)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainA, 5, false);

  const terrain::TerrainStreamer::FocusFor farFocus = [](const uuids::uuid&, const terrain::TerrainDescription&)
  {
    return glm::vec3(4.0f * terrain::chunkSize + 16.0f, 16.0f, 16.0f);
  };

  terrain::TerrainStreamer streamer;
  streamer.start(1, store, farFocus);
  streamer.start(2, store, originFocus);

  Recorder recorder;
  streamer.pump(store, recorder.sender());

  ASSERT_EQ(recorder.from(1).size(), 1u);
  ASSERT_EQ(recorder.from(2).size(), 1u);
  EXPECT_EQ(recorder.from(1).front().payload.entries.front().coord.x, 4);
  EXPECT_EQ(recorder.from(2).front().payload.entries.front().coord.x, 0);
}

TEST(TerrainStreamerTest, AViewStoreEndsUpHoldingTheServersChunks)
{
  terrain::TerrainStore server;
  addTerrain(server, terrainA, 5);
  server.find(terrainA)->setChunk({ -3, 2, 1 }, solid(4));
  server.find(terrainA)->setChunk({ 9, 0, 0 }, solid(5));
  server.stampAllChunks(terrainA);

  terrain::TerrainStore view;
  ASSERT_NE(view.create({ .uuid = terrainA }), nullptr);

  terrain::TerrainStreamer streamer;
  streamer.start(1, server, originFocus);

  const auto toView = [&view](const int32_t, const net::Message& message)
  {
    const auto payload = terrain::unpackTerrainChunks(message);
    ASSERT_TRUE(payload.has_value());

    const auto result = terrain::applyTerrainChunks(view, *payload);
    EXPECT_EQ(result.applied, payload->entries.size());
    EXPECT_TRUE(result.missing.empty());
  };

  for (int i = 0; i < 100 && streamer.streaming(1); ++i)
  {
    streamer.pump(server, toView, terrain::defaultStreamBudgetBytes);
  }

  const auto serverVolume = server.find(terrainA);
  const auto viewVolume = view.find(terrainA);
  ASSERT_EQ(viewVolume->chunks().size(), serverVolume->chunks().size());
  ASSERT_EQ(viewVolume->chunks().size(), 7u);

  for (const auto& [coord, chunk] : serverVolume->chunks())
  {
    const auto held = viewVolume->chunk(coord);
    ASSERT_NE(held, nullptr);
    EXPECT_EQ(held->hash(), chunk->hash());
    EXPECT_EQ(view.revision(terrainA, coord), server.revision(terrainA, coord));
  }
}

TEST(TerrainStreamerTest, AThrowingSendLosesNoChunkAndStillServesTheOtherConnection)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainA, 3);

  terrain::TerrainStreamer streamer;
  streamer.start(1, store, originFocus);
  streamer.start(2, store, originFocus);

  Recorder recorder;
  const auto inner = recorder.sender();
  int calls = 0;
  const terrain::TerrainStreamer::Send flaky = [&](const int32_t connId, const net::Message& message)
  {
    if (++calls == 1)
    {
      throw std::runtime_error("send failed");
    }

    inner(connId, message);
  };

  EXPECT_THROW(streamer.pump(store, flaky), std::runtime_error);

  // Connection 1 sent nothing and still holds its chunks; connection 2 was served in the same pump.
  EXPECT_TRUE(recorder.from(1).empty());
  EXPECT_EQ(streamer.pendingChunks(1), 3u);
  EXPECT_TRUE(streamer.streaming(1));
  EXPECT_EQ(chunkTotal(recorder.from(2)), 3u);

  // Connection 2's finish report from the throwing pump is returned by the next one.
  std::vector<terrain::StreamFinished> finished = streamer.pump(store, flaky);
  for (int i = 0; i < 100 && streamer.streaming(1); ++i)
  {
    const auto round = streamer.pump(store, flaky);
    finished.insert(finished.end(), round.begin(), round.end());
  }

  EXPECT_EQ(chunkTotal(recorder.from(1)), 3u);
  EXPECT_EQ(chunkTotal(recorder.from(2)), 3u);

  ASSERT_EQ(finished.size(), 2u);
  EXPECT_EQ(finished[0].connId, 2);
  EXPECT_EQ(finished[0].chunks, 3u);
  EXPECT_EQ(finished[1].connId, 1);
  EXPECT_EQ(finished[1].chunks, 3u);
}

TEST(TerrainStreamerTest, ARequeueThatRevivesAFinishedStreamReportsOnlyThatRunsCount)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainA, 3);

  terrain::TerrainStreamer streamer;
  streamer.start(1, store, originFocus);

  Recorder recorder;
  const auto first = streamer.pump(store, recorder.sender());
  ASSERT_EQ(first.size(), 1u);
  ASSERT_EQ(first.front().chunks, 3u);

  const std::vector<ChunkCoord> coords{ { 0, 0, 0 } };
  streamer.requeue(terrainA, coords);

  const auto second = streamer.pump(store, recorder.sender());
  ASSERT_EQ(second.size(), 1u);
  EXPECT_EQ(second.front().chunks, 1u);
}

TEST(TerrainStreamerTest, ARestartStartsTheSentCountOver)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainA, 4);

  terrain::TerrainStreamer streamer;
  streamer.start(1, store, originFocus);

  Recorder recorder;
  ASSERT_TRUE(streamer.pump(store, recorder.sender(), 1).empty());
  ASSERT_EQ(chunkTotal(recorder.messages), 1u);

  const auto description = *store.description(terrainA);
  const auto replacement = std::make_shared<terrain::TerrainVolume>(description.voxelSize);
  replacement->setChunk({ 40, 0, 0 }, solid());
  replacement->setChunk({ 41, 0, 0 }, solid());
  ASSERT_TRUE(store.adopt(description, replacement));
  streamer.restart(terrainA, store, originFocus);

  const auto finished = streamer.pump(store, recorder.sender());
  ASSERT_EQ(finished.size(), 1u);
  EXPECT_EQ(finished.front().chunks, 2u);
}

TEST(TerrainStreamerTest, ARestartToEmptyEndsTheStreamWithoutAReport)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainA, 4);

  terrain::TerrainStreamer streamer;
  streamer.start(1, store, originFocus);

  Recorder recorder;
  ASSERT_TRUE(streamer.pump(store, recorder.sender(), 1).empty());

  store.resetVolumes();
  streamer.restart(terrainA, store, originFocus);

  EXPECT_TRUE(streamer.pump(store, recorder.sender()).empty());

  // Positive control: a stream that sends its chunks does report.
  terrain::TerrainStore other;
  addTerrain(other, terrainA, 1);
  streamer.start(1, other, originFocus);
  EXPECT_EQ(streamer.pump(other, recorder.sender()).size(), 1u);
}

TEST(TerrainStreamerTest, AnAbsentCoordinateIsStampedAndSentAsAir)
{
  terrain::TerrainStore store;
  ASSERT_NE(store.create({ .uuid = terrainA }), nullptr);

  terrain::TerrainStreamer streamer;
  streamer.start(1, store, originFocus);

  const std::vector<ChunkCoord> coords{ { 5, 0, 0 } };
  streamer.requeue(terrainA, coords);
  ASSERT_EQ(store.revision(terrainA, coords.front()), 0u);

  Recorder recorder;
  streamer.pump(store, recorder.sender());

  ASSERT_EQ(chunkTotal(recorder.messages), 1u);
  const auto& entry = recorder.messages.front().payload.entries.front();
  EXPECT_EQ(entry.coord, coords.front());
  EXPECT_GT(entry.revision, 0u);
  EXPECT_EQ(store.revision(terrainA, coords.front()), entry.revision);
  EXPECT_EQ(store.find(terrainA)->chunk(coords.front()), nullptr);
}

TEST(TerrainStreamerTest, ForgettingAConnectionDropsItsPendingFinishReport)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainA, 1, false);

  terrain::TerrainStreamer streamer;
  streamer.start(1, store, originFocus);
  streamer.start(2, store, originFocus);

  // Connection 1 finishes in this pump, then connection 2's send throws, so the report is held back.
  Recorder recorder;
  const auto sender = recorder.sender();
  const terrain::TerrainStreamer::Send throwingFor2 = [&sender](const int32_t connId, const net::Message& message)
  {
    if (connId == 2)
    {
      throw std::runtime_error("send failed");
    }

    sender(connId, message);
  };

  EXPECT_THROW(streamer.pump(store, throwingFor2), std::runtime_error);
  ASSERT_EQ(recorder.from(1).size(), 1u);

  streamer.forget(1);

  const auto finished = streamer.pump(store, recorder.sender());

  EXPECT_TRUE(std::ranges::none_of(finished, [](const terrain::StreamFinished& report) { return report.connId == 1; }));

  // Positive control: the connection that was not forgotten is still reported.
  ASSERT_EQ(finished.size(), 1u);
  EXPECT_EQ(finished.front().connId, 2);
}

TEST(TerrainVolumeSnapshotTest, AnUnchangedStoreReplacesNothing)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainA, 2);
  addTerrain(store, terrainB, 2);

  const auto before = terrain::captureVolumes(store);

  EXPECT_EQ(before.size(), 2u);
  EXPECT_TRUE(terrain::replacedVolumes(before, store).empty());

  // Changing a chunk inside the same volume is not a replacement.
  store.find(terrainA)->setChunk({ 0, 0, 0 }, solid());
  EXPECT_TRUE(terrain::replacedVolumes(before, store).empty());
}

TEST(TerrainVolumeSnapshotTest, AnAddedTerrainIsReplaced)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainA, 2);

  const auto before = terrain::captureVolumes(store);
  addTerrain(store, terrainB, 2);

  EXPECT_EQ(terrain::replacedVolumes(before, store), std::vector<uuids::uuid>{ terrainB });
}

TEST(TerrainVolumeSnapshotTest, ARemovedTerrainIsReplaced)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainA, 2);
  addTerrain(store, terrainB, 2);

  const auto before = terrain::captureVolumes(store);
  ASSERT_TRUE(store.remove(terrainA));

  EXPECT_EQ(terrain::replacedVolumes(before, store), std::vector<uuids::uuid>{ terrainA });
}

TEST(TerrainVolumeSnapshotTest, ARemoveAndReAddUnderOneUuidIsReplaced)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainA, 2);
  addTerrain(store, terrainB, 2);

  const auto before = terrain::captureVolumes(store);
  ASSERT_TRUE(store.remove(terrainA));
  ASSERT_NE(store.create({ .uuid = terrainA }), nullptr);

  EXPECT_EQ(terrain::replacedVolumes(before, store), std::vector<uuids::uuid>{ terrainA });
}

TEST(TerrainVolumeSnapshotTest, ResetVolumesReplacesEveryTerrainInUuidOrder)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainB, 2);
  addTerrain(store, terrainA, 2);

  const auto before = terrain::captureVolumes(store);
  store.resetVolumes();

  const std::vector<uuids::uuid> expected{ terrainA, terrainB };
  EXPECT_EQ(terrain::replacedVolumes(before, store), expected);
}

TEST(TerrainVolumeSnapshotTest, AnAddAndARemoveTogetherListBoth)
{
  terrain::TerrainStore store;
  addTerrain(store, terrainA, 2);

  const auto before = terrain::captureVolumes(store);
  ASSERT_TRUE(store.remove(terrainA));
  addTerrain(store, terrainB, 2);

  const std::vector<uuids::uuid> expected{ terrainA, terrainB };
  EXPECT_EQ(terrain::replacedVolumes(before, store), expected);
}
