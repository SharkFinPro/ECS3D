#include <gtest/gtest.h>

#include "terrain/TerrainChunk.h"
#include "terrain/TerrainCodec.h"
#include "terrain/TerrainReplication.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainUpload.h"
#include "terrain/TerrainVolume.h"

#include <Protocol.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_set>
#include <stdexcept>
#include <vector>

#include "TerrainStreamerFixtures.h"

using namespace terrainStreamerFixtures;

namespace {
  constexpr size_t unlimitedBudget = 100 * 1024 * 1024;

  std::shared_ptr<terrain::TerrainVolume> makeVolume(const int count, const bool dense = true)
  {
    auto volume = std::make_shared<terrain::TerrainVolume>(2.5f);

    for (int x = 0; x < count; ++x)
    {
      volume->setChunk({ x, 0, 0 }, dense ? noise(static_cast<uint32_t>(x)) : solid());
    }

    return volume;
  }

  struct Sent
  {
    size_t bytes = 0;
    terrain::TerrainChunksPayload payload;
  };

  struct UploadRecorder
  {
    std::vector<Sent> messages;

    std::function<void(const net::Message&)> sender()
    {
      return [this](const net::Message& message)
      {
        const auto payload = terrain::unpackTerrainChunks(message);
        ASSERT_TRUE(payload.has_value());
        messages.push_back({ message.size(), *payload });
      };
    }

    [[nodiscard]] size_t chunkTotal() const
    {
      size_t total = 0;
      for (const Sent& sent : messages)
      {
        total += sent.payload.entries.size();
      }

      return total;
    }
  };

  terrain::TerrainChunksPayload fullPayload(const uuids::uuid& uuid, const int count, const uint32_t revision = 1)
  {
    terrain::TerrainChunksPayload payload;
    payload.terrain = uuid;

    for (int x = 0; x < count; ++x)
    {
      const auto chunk = noise(static_cast<uint32_t>(x));
      payload.entries.push_back({ { x, 0, 0 }, revision, chunk->hash(), chunk });
    }

    return payload;
  }
}

TEST(TerrainUpload, EveryChunkIsSentExactlyOnceWithTheVolumesHashesAndRevisionOne)
{
  const auto volume = makeVolume(5);
  terrain::TerrainUpload upload;
  upload.add(terrainA, volume);
  EXPECT_FALSE(upload.done());
  EXPECT_EQ(upload.pendingChunks(), 5u);

  UploadRecorder recorder;
  EXPECT_EQ(upload.pump(recorder.sender(), unlimitedBudget), 5u);

  ASSERT_EQ(recorder.chunkTotal(), 5u);
  std::unordered_set<terrain::ChunkCoord> seen;
  for (const Sent& sent : recorder.messages)
  {
    EXPECT_EQ(sent.payload.terrain, terrainA);

    for (const terrain::ReceivedChunk& entry : sent.payload.entries)
    {
      EXPECT_TRUE(seen.insert(entry.coord).second);
      ASSERT_NE(volume->chunk(entry.coord), nullptr);
      EXPECT_EQ(entry.hash, volume->chunk(entry.coord)->hash());
      EXPECT_EQ(entry.revision, 1u);
      ASSERT_NE(entry.chunk, nullptr);
    }
  }

  EXPECT_EQ(seen.size(), 5u);
  EXPECT_TRUE(upload.done());
  EXPECT_EQ(upload.pendingChunks(), 0u);

  EXPECT_EQ(upload.pump(recorder.sender()), 0u);
  EXPECT_EQ(recorder.chunkTotal(), 5u);
}

TEST(TerrainUpload, TerrainsAreSentInUuidOrderWhateverTheAddOrder)
{
  terrain::TerrainUpload upload;
  upload.add(terrainB, makeVolume(2));
  upload.add(terrainA, makeVolume(2));

  UploadRecorder recorder;
  upload.pump(recorder.sender(), unlimitedBudget);

  ASSERT_EQ(recorder.messages.size(), 2u);
  EXPECT_EQ(recorder.messages[0].payload.terrain, terrainA);
  EXPECT_EQ(recorder.messages[1].payload.terrain, terrainB);
}

TEST(TerrainUpload, TheBudgetLimitsAPumpButTheFirstChunkAlwaysGoes)
{
  terrain::TerrainUpload upload;
  upload.add(terrainA, makeVolume(6));

  UploadRecorder recorder;
  EXPECT_EQ(upload.pump(recorder.sender(), 1), 1u);
  EXPECT_EQ(upload.pendingChunks(), 5u);
  ASSERT_EQ(recorder.messages.size(), 1u);
  EXPECT_GT(recorder.messages[0].bytes, 1u);

  const size_t oneChunk = terrain::streamEntryOverheadBytes + terrain::encodeChunk(*noise(0)).size();
  const size_t budget = terrain::streamMessageOverheadBytes + 2 * oneChunk + oneChunk / 2;

  size_t pumps = 0;
  while (!upload.done() && pumps < 100)
  {
    const size_t sent = upload.pump(recorder.sender(), budget);
    EXPECT_GE(sent, 1u);
    EXPECT_LE(sent, 3u);
    ++pumps;
  }

  EXPECT_TRUE(upload.done());
  EXPECT_EQ(recorder.chunkTotal(), 6u);
  EXPECT_GT(pumps, 1u);
}

TEST(TerrainUpload, AMessageHoldsAtMostTheEntryCap)
{
  const size_t total = terrain::maxTerrainChunksEntries + 476;
  terrain::TerrainUpload upload;
  upload.add(terrainA, makeVolume(static_cast<int>(total), false));

  UploadRecorder recorder;
  EXPECT_EQ(upload.pump(recorder.sender(), unlimitedBudget), total);

  ASSERT_EQ(recorder.messages.size(), 2u);
  EXPECT_EQ(recorder.messages[0].payload.entries.size(), terrain::maxTerrainChunksEntries);
  EXPECT_EQ(recorder.messages[1].payload.entries.size(), 476u);
}

TEST(TerrainUpload, ClearDropsEverythingQueued)
{
  terrain::TerrainUpload upload;
  upload.add(terrainA, makeVolume(3));
  ASSERT_EQ(upload.pendingChunks(), 3u);

  upload.clear();

  UploadRecorder recorder;
  EXPECT_TRUE(upload.done());
  EXPECT_EQ(upload.pendingChunks(), 0u);
  EXPECT_EQ(upload.pump(recorder.sender()), 0u);
  EXPECT_TRUE(recorder.messages.empty());

  upload.add(terrainA, makeVolume(3));
  EXPECT_EQ(upload.pump(recorder.sender(), unlimitedBudget), 3u);
}

TEST(TerrainUpload, AThrowingSendLosesNothingAndRethrows)
{
  terrain::TerrainUpload upload;
  upload.add(terrainA, makeVolume(4));

  const auto failing = [](const net::Message&) { throw std::runtime_error("down"); };
  EXPECT_THROW(upload.pump(failing, unlimitedBudget), std::runtime_error);
  EXPECT_EQ(upload.pendingChunks(), 4u);

  UploadRecorder recorder;
  EXPECT_EQ(upload.pump(recorder.sender(), unlimitedBudget), 4u);

  std::unordered_set<terrain::ChunkCoord> seen;
  for (const terrain::ReceivedChunk& entry : recorder.messages.at(0).payload.entries)
  {
    seen.insert(entry.coord);
  }

  EXPECT_EQ(seen.size(), 4u);
}

TEST(TerrainUpload, ASendThatFailsAfterAnEarlierBatchWentOnlyRequeuesTheFailedBatch)
{
  const size_t total = terrain::maxTerrainChunksEntries + 10;
  terrain::TerrainUpload upload;
  upload.add(terrainA, makeVolume(static_cast<int>(total), false));

  int calls = 0;
  UploadRecorder recorder;
  const auto good = recorder.sender();
  const auto flaky = [&](const net::Message& message)
  {
    if (++calls == 2)
    {
      throw std::runtime_error("down");
    }

    good(message);
  };

  EXPECT_THROW(upload.pump(flaky, unlimitedBudget), std::runtime_error);
  EXPECT_EQ(recorder.chunkTotal(), terrain::maxTerrainChunksEntries);
  EXPECT_EQ(upload.pendingChunks(), 10u);

  EXPECT_EQ(upload.pump(recorder.sender(), unlimitedBudget), 10u);
  EXPECT_EQ(recorder.chunkTotal(), total);
}

TEST(TerrainApplyUpload, FullEntriesAreSetWithTheServersOwnRevisions)
{
  terrain::TerrainStore store;
  ASSERT_NE(store.create({ .uuid = terrainA }), nullptr);
  ASSERT_TRUE(store.expectChunks(terrainA, 3));

  const auto result = terrain::applyTerrainUpload(store, fullPayload(terrainA, 2, 77));

  EXPECT_FALSE(result.unknownTerrain);
  EXPECT_FALSE(result.notExpected);
  EXPECT_FALSE(result.completed);
  EXPECT_EQ(result.ignoredHashOnly, 0u);
  ASSERT_EQ(result.applied.size(), 2u);
  EXPECT_EQ(store.find(terrainA)->chunks().size(), 2u);
  EXPECT_EQ(store.expectedChunks(terrainA), std::optional<uint32_t>(3));

  for (const terrain::ChunkCoord coord : result.applied)
  {
    EXPECT_NE(store.revision(terrainA, coord), 77u);
    EXPECT_GT(store.revision(terrainA, coord), 0u);
  }

  EXPECT_NE(store.revision(terrainA, result.applied[0]), store.revision(terrainA, result.applied[1]));
}

TEST(TerrainApplyUpload, TheMessageThatReachesTheExpectedCountCompletesAndClearsIt)
{
  terrain::TerrainStore store;
  ASSERT_NE(store.create({ .uuid = terrainA }), nullptr);
  ASSERT_TRUE(store.expectChunks(terrainA, 3));

  EXPECT_FALSE(terrain::applyTerrainUpload(store, fullPayload(terrainA, 2)).completed);

  terrain::TerrainChunksPayload last;
  last.terrain = terrainA;
  const auto chunk = noise(9);
  last.entries.push_back({ { 2, 0, 0 }, 1, chunk->hash(), chunk });

  const auto result = terrain::applyTerrainUpload(store, last);

  EXPECT_TRUE(result.completed);
  EXPECT_EQ(result.applied.size(), 1u);
  EXPECT_FALSE(store.expectedChunks(terrainA).has_value());
  EXPECT_EQ(store.find(terrainA)->chunks().size(), 3u);
}

TEST(TerrainApplyUpload, AnUnknownTerrainChangesNothing)
{
  terrain::TerrainStore store;
  ASSERT_NE(store.create({ .uuid = terrainA }), nullptr);
  ASSERT_TRUE(store.expectChunks(terrainA, 2));

  const auto result = terrain::applyTerrainUpload(store, fullPayload(terrainB, 2));

  EXPECT_TRUE(result.unknownTerrain);
  EXPECT_TRUE(result.applied.empty());
  EXPECT_FALSE(result.completed);
  EXPECT_TRUE(store.find(terrainA)->chunks().empty());
  EXPECT_EQ(store.expectedChunks(terrainA), std::optional<uint32_t>(2));
}

TEST(TerrainApplyUpload, ATerrainWithoutAPendingExpectationIsNotWritten)
{
  terrain::TerrainStore store;
  ASSERT_NE(store.create({ .uuid = terrainA }), nullptr);

  const auto refused = terrain::applyTerrainUpload(store, fullPayload(terrainA, 2));

  EXPECT_TRUE(refused.notExpected);
  EXPECT_FALSE(refused.unknownTerrain);
  EXPECT_TRUE(refused.applied.empty());
  EXPECT_FALSE(refused.completed);
  EXPECT_TRUE(store.find(terrainA)->chunks().empty());

  ASSERT_TRUE(store.expectChunks(terrainA, 2));
  const auto accepted = terrain::applyTerrainUpload(store, fullPayload(terrainA, 2));

  EXPECT_FALSE(accepted.notExpected);
  EXPECT_EQ(accepted.applied.size(), 2u);
  EXPECT_EQ(store.find(terrainA)->chunks().size(), 2u);
}

TEST(TerrainApplyUpload, HashOnlyEntriesAreIgnoredAndCounted)
{
  terrain::TerrainStore store;
  ASSERT_NE(store.create({ .uuid = terrainA }), nullptr);
  ASSERT_TRUE(store.expectChunks(terrainA, 5));

  auto payload = fullPayload(terrainA, 1);
  payload.entries.push_back({ { 5, 0, 0 }, 1, 123, nullptr });
  payload.entries.push_back({ { 6, 0, 0 }, 1, 456, nullptr });

  const auto result = terrain::applyTerrainUpload(store, payload);

  EXPECT_EQ(result.ignoredHashOnly, 2u);
  ASSERT_EQ(result.applied.size(), 1u);
  EXPECT_EQ(result.applied[0], (terrain::ChunkCoord{ 0, 0, 0 }));
  EXPECT_EQ(store.find(terrainA)->chunks().size(), 1u);
  EXPECT_EQ(store.revision(terrainA, { 5, 0, 0 }), 0u);
}

TEST(TerrainApplyUpload, AnUploadFromTerrainUploadReproducesTheSendersVolume)
{
  const auto source = makeVolume(6);
  terrain::TerrainUpload upload;
  upload.add(terrainA, source);

  terrain::TerrainStore server;
  ASSERT_NE(server.create({ .uuid = terrainA }), nullptr);
  ASSERT_TRUE(server.expectChunks(terrainA, 6));

  bool completed = false;
  size_t guard = 0;
  while (!upload.done() && guard++ < 100)
  {
    upload.pump([&](const net::Message& message)
    {
      const auto payload = terrain::unpackTerrainChunks(message);
      ASSERT_TRUE(payload.has_value());
      completed = terrain::applyTerrainUpload(server, *payload).completed;
    }, 1);
  }

  EXPECT_TRUE(completed);
  ASSERT_EQ(server.find(terrainA)->chunks().size(), 6u);
  for (const auto& [coord, chunk] : source->chunks())
  {
    ASSERT_NE(server.find(terrainA)->chunk(coord), nullptr);
    EXPECT_EQ(server.find(terrainA)->chunk(coord)->hash(), chunk->hash());
  }
}

TEST(TerrainPendingUpload, NothingIsPendingWithoutAnExpectation)
{
  terrain::TerrainStore store;
  EXPECT_FALSE(terrain::pendingUploadReason(store).has_value());

  ASSERT_NE(store.create({ .uuid = terrainA }), nullptr);
  EXPECT_FALSE(terrain::pendingUploadReason(store).has_value());
}

TEST(TerrainPendingUpload, TheReasonNamesTheFirstShortTerrainAndItsCounts)
{
  terrain::TerrainStore store;
  ASSERT_NE(store.create({ .uuid = terrainB }), nullptr);
  ASSERT_NE(store.create({ .uuid = terrainA }), nullptr);
  ASSERT_TRUE(store.expectChunks(terrainB, 9));
  ASSERT_TRUE(store.expectChunks(terrainA, 5));
  store.find(terrainA)->setChunk({ 0, 0, 0 }, solid());
  store.find(terrainA)->setChunk({ 1, 0, 0 }, solid());

  EXPECT_EQ(terrain::pendingUploadReason(store),
            "Play waits for terrain " + uuids::to_string(terrainA) + ": 2 of 5 chunk(s) uploaded");

  store.clearExpectation(terrainA);
  EXPECT_EQ(terrain::pendingUploadReason(store),
            "Play waits for terrain " + uuids::to_string(terrainB) + ": 0 of 9 chunk(s) uploaded");

  store.clearExpectation(terrainB);
  EXPECT_FALSE(terrain::pendingUploadReason(store).has_value());
}
