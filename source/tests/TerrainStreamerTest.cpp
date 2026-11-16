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
#include <random>
#include <utility>
#include <vector>

namespace {
  using terrain::ChunkCoord;

  const auto terrainA = uuids::uuid::from_string("11111111-1111-1111-1111-111111111111").value();
  const auto terrainB = uuids::uuid::from_string("22222222-2222-2222-2222-222222222222").value();

  std::shared_ptr<const terrain::TerrainChunk> solid(const uint8_t material = 1)
  {
    return terrain::TerrainChunk::uniform(-terrain::airDistance, material);
  }

  std::shared_ptr<const terrain::TerrainChunk> noise(const uint32_t seed)
  {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> distance(-terrain::airDistance, terrain::airDistance);
    std::vector<int16_t> distances(terrain::samplesPerChunk);
    std::vector<uint8_t> materials(terrain::samplesPerChunk, 1);

    for (auto& value : distances)
    {
      value = static_cast<int16_t>(distance(rng));
    }

    return terrain::TerrainChunk::dense(std::move(distances), std::move(materials));
  }

  // Stamps every chunk, as the server does after loading terrain files.
  void addTerrain(terrain::TerrainStore& store, const uuids::uuid& uuid, const int count, const bool dense = true)
  {
    const auto volume = store.create({ .uuid = uuid });
    ASSERT_NE(volume, nullptr);

    for (int x = 0; x < count; ++x)
    {
      volume->setChunk({ x, 0, 0 }, dense ? noise(static_cast<uint32_t>(x)) : solid());
    }

    store.stampAllChunks(uuid);
  }

  struct Received
  {
    int32_t connId = 0;
    size_t bytes = 0;
    terrain::TerrainChunksPayload payload;
  };

  struct Recorder
  {
    std::vector<Received> messages;

    terrain::TerrainStreamer::Send sender()
    {
      return [this](const int32_t connId, const net::Message& message)
      {
        const auto payload = terrain::unpackTerrainChunks(message);
        ASSERT_TRUE(payload.has_value());
        messages.push_back({ connId, message.size(), *payload });
      };
    }

    [[nodiscard]] std::vector<Received> from(const int32_t connId) const
    {
      std::vector<Received> found;
      std::ranges::copy_if(messages, std::back_inserter(found),
                           [connId](const Received& received) { return received.connId == connId; });
      return found;
    }
  };

  const terrain::TerrainStreamer::FocusFor originFocus =
    [](const uuids::uuid&, const terrain::TerrainDescription&) { return glm::vec3(16.0f); };

  size_t chunkTotal(const std::vector<Received>& messages)
  {
    size_t total = 0;
    for (const Received& received : messages)
    {
      total += received.payload.entries.size();
    }

    return total;
  }

  void pumpUntilDone(terrain::TerrainStreamer& streamer, terrain::TerrainStore& store, Recorder& recorder,
                     const size_t budget = terrain::defaultStreamBudgetBytes)
  {
    for (int i = 0; i < 10000 && streamer.streaming(1); ++i)
    {
      streamer.pump(store, recorder.sender(), budget);
    }
  }
}

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

  while (streamer.streaming(1) || streamer.streaming(2))
  {
    streamer.pump(store, recorder.sender(), 1);
  }

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
