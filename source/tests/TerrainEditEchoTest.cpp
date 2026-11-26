#include <gtest/gtest.h>

#include "scenes/SceneManager.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainEditEcho.h"
#include "terrain/TerrainReplication.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainViewSync.h"

#include <Protocol.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

#include "TerrainStreamerFixtures.h"

using namespace terrainStreamerFixtures;

namespace
{
  const std::vector<int32_t> bothConnections{ 1, 2 };

  // Distinct content per index, never air.
  std::shared_ptr<const terrain::TerrainChunk> variant(const int index)
  {
    return terrain::TerrainChunk::uniform(static_cast<int16_t>(-100 - index), 1);
  }

  void makeTerrain(terrain::TerrainStore& store)
  {
    ASSERT_NE(store.create({ .uuid = terrainA }), nullptr);
    // The server's first edit then lands above the revision 1 a view holds.
    ASSERT_EQ(store.nextRevision(terrainA), 1u);
  }

  // The server applying an edit: the new content under a fresh revision.
  void serverSet(terrain::TerrainStore& store, const ChunkCoord coord,
                 const std::shared_ptr<const terrain::TerrainChunk>& chunk)
  {
    ASSERT_TRUE(store.setChunk(terrainA, coord, chunk, store.nextRevision(terrainA)));
  }

  // A view holding content under revision 1, below anything the server hands out afterward.
  void viewHolds(terrain::TerrainStore& store, const ChunkCoord coord,
                 const std::shared_ptr<const terrain::TerrainChunk>& chunk)
  {
    ASSERT_NE(store.create({ .uuid = terrainA }), nullptr);
    ASSERT_TRUE(store.setChunk(terrainA, coord, chunk, 1));
  }

  void flush(terrain::TerrainEditEcho& echo, terrain::TerrainStore& store, Recorder& recorder,
             const std::vector<int32_t>& connections = bothConnections,
             const size_t budget = terrain::defaultStreamBudgetBytes)
  {
    echo.flush(store, connections, recorder.sender(), budget);
  }

  uint64_t heldHash(const terrain::TerrainStore& store, const ChunkCoord coord)
  {
    return store.find(terrainA)->chunk(coord)->hash();
  }
}

TEST(TerrainEditEchoTest, ManyEditsToOneChunkGoOutOnceWithTheLatestState)
{
  terrain::TerrainStore store;
  makeTerrain(store);
  terrain::TerrainEditEcho echo;
  const ChunkCoord coord{ 2, 0, 0 };

  for (int i = 0; i < 10; ++i)
  {
    serverSet(store, coord, variant(i));
    const std::vector<ChunkCoord> coords{ coord };
    echo.recordEdit(terrainA, coords, 1);
  }

  ASSERT_TRUE(echo.pending());
  Recorder recorder;
  flush(echo, store, recorder);

  const auto sender = recorder.from(1);
  const auto other = recorder.from(2);
  ASSERT_EQ(sender.size(), 1u);
  ASSERT_EQ(other.size(), 1u);
  ASSERT_EQ(sender[0].payload.entries.size(), 1u);
  ASSERT_EQ(other[0].payload.entries.size(), 1u);

  const uint32_t revision = store.revision(terrainA, coord);
  EXPECT_EQ(sender[0].payload.terrain, terrainA);
  EXPECT_EQ(sender[0].payload.entries[0].hash, variant(9)->hash());
  EXPECT_EQ(sender[0].payload.entries[0].revision, revision);
  EXPECT_EQ(other[0].payload.entries[0].hash, variant(9)->hash());
  EXPECT_EQ(other[0].payload.entries[0].revision, revision);
}

TEST(TerrainEditEchoTest, TheSenderGetsHashOnlyAndEveryoneElseTheFullChunk)
{
  terrain::TerrainStore server;
  makeTerrain(server);
  const ChunkCoord coord{ 0, 0, 0 };
  serverSet(server, coord, noise(7));

  terrain::TerrainEditEcho echo;
  const std::vector<ChunkCoord> coords{ coord };
  echo.recordEdit(terrainA, coords, 1);
  Recorder recorder;
  flush(echo, server, recorder);

  terrain::TerrainStore predicted;
  viewHolds(predicted, coord, noise(7));
  terrain::TerrainStore stale;
  viewHolds(stale, coord, noise(3));

  const auto sender = recorder.from(1);
  const auto other = recorder.from(2);
  ASSERT_EQ(sender.size(), 1u);
  ASSERT_EQ(other.size(), 1u);
  ASSERT_EQ(sender[0].payload.entries.size(), 1u);
  ASSERT_EQ(other[0].payload.entries.size(), 1u);
  EXPECT_EQ(sender[0].payload.entries[0].chunk, nullptr);
  EXPECT_NE(other[0].payload.entries[0].chunk, nullptr);
  EXPECT_LT(sender[0].bytes, other[0].bytes);

  const auto predictedResult = terrain::applyTerrainChunks(predicted, sender[0].payload);
  EXPECT_EQ(predictedResult.applied, 1u);
  EXPECT_TRUE(predictedResult.missing.empty());
  const auto staleResult = terrain::applyTerrainChunks(stale, other[0].payload);
  EXPECT_EQ(staleResult.applied, 1u);

  EXPECT_EQ(heldHash(predicted, coord), heldHash(server, coord));
  EXPECT_EQ(heldHash(stale, coord), heldHash(server, coord));
  EXPECT_EQ(predicted.revision(terrainA, coord), server.revision(terrainA, coord));
  EXPECT_EQ(stale.revision(terrainA, coord), server.revision(terrainA, coord));
}

TEST(TerrainEditEchoTest, AWrongPredictionIsRepairedByARequestForTheFullChunk)
{
  terrain::TerrainStore server;
  makeTerrain(server);
  const ChunkCoord coord{ 1, 1, 0 };
  serverSet(server, coord, noise(9));

  terrain::TerrainEditEcho echo;
  const std::vector<ChunkCoord> coords{ coord };
  echo.recordEdit(terrainA, coords, 1);
  Recorder first;
  flush(echo, server, first);

  terrain::TerrainStore view;
  viewHolds(view, coord, noise(4));
  SceneManager scenes;
  terrain::TerrainViewSync sync;

  const auto told = first.from(1);
  ASSERT_EQ(told.size(), 1u);
  const auto outcome = sync.apply(view, scenes, terrain::packTerrainChunks(
    terrainA, std::vector<terrain::ChunkEntry>{ terrain::makeHashOnlyEntry(
      coord, told[0].payload.entries[0].revision, told[0].payload.entries[0].hash) }));

  ASSERT_EQ(outcome.result.missing.size(), 1u);
  ASSERT_TRUE(outcome.request.has_value());
  const auto request = terrain::unpackTerrainChunks(*outcome.request);
  ASSERT_TRUE(request.has_value());
  EXPECT_TRUE(terrain::isChunkRequest(*request));
  ASSERT_EQ(request->entries.size(), 1u);
  EXPECT_EQ(request->entries[0].coord, coord);
  EXPECT_EQ(request->entries[0].revision, told[0].payload.entries[0].revision);
  EXPECT_EQ(request->entries[0].hash, told[0].payload.entries[0].hash);
  EXPECT_NE(heldHash(view, coord), heldHash(server, coord));

  echo.requestFull(terrainA, coords, 1);
  Recorder second;
  flush(echo, server, second);

  EXPECT_TRUE(second.from(2).empty());
  const auto repair = second.from(1);
  ASSERT_EQ(repair.size(), 1u);
  ASSERT_EQ(repair[0].payload.entries.size(), 1u);
  EXPECT_NE(repair[0].payload.entries[0].chunk, nullptr);

  EXPECT_EQ(terrain::applyTerrainChunks(view, repair[0].payload).applied, 1u);
  EXPECT_EQ(heldHash(view, coord), heldHash(server, coord));
}

TEST(TerrainEditEchoTest, TwoSendersOnOneChunkBothGetItInFull)
{
  terrain::TerrainStore store;
  makeTerrain(store);
  const ChunkCoord coord{ 0, 0, 0 };
  serverSet(store, coord, variant(1));

  terrain::TerrainEditEcho echo;
  const std::vector<ChunkCoord> coords{ coord };
  echo.recordEdit(terrainA, coords, 1);
  echo.recordEdit(terrainA, coords, 2);
  Recorder recorder;
  flush(echo, store, recorder);

  for (const int32_t connId : bothConnections)
  {
    const auto got = recorder.from(connId);
    ASSERT_EQ(got.size(), 1u);
    ASSERT_EQ(got[0].payload.entries.size(), 1u);
    EXPECT_NE(got[0].payload.entries[0].chunk, nullptr);
  }

  // Positive control: one sender alone gets hash-only.
  serverSet(store, coord, variant(2));
  echo.recordEdit(terrainA, coords, 1);
  Recorder alone;
  flush(echo, store, alone);
  ASSERT_EQ(alone.from(1).size(), 1u);
  EXPECT_EQ(alone.from(1)[0].payload.entries[0].chunk, nullptr);
}

TEST(TerrainEditEchoTest, SendersOnDifferentChunksEachGetTheirOwnHashOnlyAndTheOthersInFull)
{
  terrain::TerrainStore store;
  makeTerrain(store);
  const ChunkCoord first{ 0, 0, 0 };
  const ChunkCoord second{ 1, 0, 0 };
  serverSet(store, first, variant(1));
  serverSet(store, second, variant(2));

  terrain::TerrainEditEcho echo;
  echo.recordEdit(terrainA, std::vector<ChunkCoord>{ second }, 2);
  echo.recordEdit(terrainA, std::vector<ChunkCoord>{ first }, 1);
  Recorder recorder;
  flush(echo, store, recorder);

  const auto one = recorder.from(1);
  const auto two = recorder.from(2);
  ASSERT_EQ(one.size(), 1u);
  ASSERT_EQ(two.size(), 1u);
  ASSERT_EQ(one[0].payload.entries.size(), 2u);
  ASSERT_EQ(two[0].payload.entries.size(), 2u);

  EXPECT_EQ(one[0].payload.entries[0].coord, first);
  EXPECT_EQ(one[0].payload.entries[0].chunk, nullptr);
  EXPECT_EQ(one[0].payload.entries[1].coord, second);
  EXPECT_NE(one[0].payload.entries[1].chunk, nullptr);

  EXPECT_EQ(two[0].payload.entries[0].coord, first);
  EXPECT_NE(two[0].payload.entries[0].chunk, nullptr);
  EXPECT_EQ(two[0].payload.entries[1].coord, second);
  EXPECT_EQ(two[0].payload.entries[1].chunk, nullptr);
}

TEST(TerrainEditEchoTest, ChunksGoOutInZYXOrder)
{
  terrain::TerrainStore store;
  makeTerrain(store);
  const std::vector<ChunkCoord> coords{ { 1, 0, 1 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, { 0, 0, 0 } };

  for (const ChunkCoord coord : coords)
  {
    serverSet(store, coord, variant(coord.x + coord.y + coord.z));
  }

  terrain::TerrainEditEcho echo;
  echo.recordEdit(terrainA, coords, 1);
  Recorder recorder;
  flush(echo, store, recorder, { 2 });

  const auto got = recorder.from(2);
  ASSERT_EQ(got.size(), 1u);
  const std::vector<ChunkCoord> expected{ { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 1, 0, 1 } };
  ASSERT_EQ(got[0].payload.entries.size(), expected.size());

  for (size_t i = 0; i < expected.size(); ++i)
  {
    EXPECT_EQ(got[0].payload.entries[i].coord, expected[i]);
  }
}

TEST(TerrainEditEchoTest, ARequestGoesOnlyToItsRequester)
{
  terrain::TerrainStore store;
  makeTerrain(store);
  const ChunkCoord coord{ 0, 0, 0 };
  serverSet(store, coord, variant(1));

  terrain::TerrainEditEcho echo;
  const std::vector<ChunkCoord> coords{ coord };
  echo.requestFull(terrainA, coords, 1);
  ASSERT_TRUE(echo.pending());
  Recorder recorder;
  flush(echo, store, recorder);

  EXPECT_FALSE(echo.pending());
  EXPECT_TRUE(recorder.from(2).empty());
  ASSERT_EQ(recorder.from(1).size(), 1u);
  ASSERT_EQ(recorder.from(1)[0].payload.entries.size(), 1u);
  EXPECT_NE(recorder.from(1)[0].payload.entries[0].chunk, nullptr);
  EXPECT_EQ(recorder.from(1)[0].payload.entries[0].hash, variant(1)->hash());
}

TEST(TerrainEditEchoTest, ARequesterThatIsNotListedLosesItsRequest)
{
  terrain::TerrainStore store;
  makeTerrain(store);
  const ChunkCoord coord{ 0, 0, 0 };
  serverSet(store, coord, variant(1));

  terrain::TerrainEditEcho echo;
  const std::vector<ChunkCoord> coords{ coord };
  echo.requestFull(terrainA, coords, 1);
  Recorder none;
  flush(echo, store, none, { 2 });
  EXPECT_TRUE(none.messages.empty());
  EXPECT_FALSE(echo.pending());

  // Positive control: the same request is served when its requester is listed.
  echo.requestFull(terrainA, coords, 1);
  Recorder listed;
  flush(echo, store, listed, { 1 });
  EXPECT_EQ(listed.from(1).size(), 1u);
}

TEST(TerrainEditEchoTest, ForgetDropsARequest)
{
  terrain::TerrainStore store;
  makeTerrain(store);
  const ChunkCoord coord{ 0, 0, 0 };
  serverSet(store, coord, variant(1));

  terrain::TerrainEditEcho echo;
  const std::vector<ChunkCoord> coords{ coord };
  echo.requestFull(terrainA, coords, 1);
  echo.requestFull(terrainA, coords, 2);
  echo.forget(1);

  Recorder recorder;
  flush(echo, store, recorder);

  EXPECT_TRUE(recorder.from(1).empty());
  EXPECT_EQ(recorder.from(2).size(), 1u);
}

TEST(TerrainEditEchoTest, AForgottenSenderStillHasItsChunksEchoedToEveryoneElse)
{
  terrain::TerrainStore store;
  makeTerrain(store);
  const ChunkCoord coord{ 0, 0, 0 };
  serverSet(store, coord, variant(1));

  terrain::TerrainEditEcho echo;
  echo.recordEdit(terrainA, std::vector<ChunkCoord>{ coord }, 1);
  echo.forget(1);
  ASSERT_TRUE(echo.pending());

  Recorder recorder;
  flush(echo, store, recorder, { 2, 3 });

  for (const int32_t connId : { 2, 3 })
  {
    const auto got = recorder.from(connId);
    ASSERT_EQ(got.size(), 1u);
    ASSERT_EQ(got[0].payload.entries.size(), 1u);
    EXPECT_NE(got[0].payload.entries[0].chunk, nullptr);
  }
}

TEST(TerrainEditEchoTest, ARecordedAndRequestedChunkGoesOnceInFull)
{
  terrain::TerrainStore store;
  makeTerrain(store);
  const ChunkCoord coord{ 0, 0, 0 };
  serverSet(store, coord, variant(1));

  terrain::TerrainEditEcho echo;
  const std::vector<ChunkCoord> coords{ coord };
  echo.recordEdit(terrainA, coords, 1);
  echo.requestFull(terrainA, coords, 1);
  Recorder recorder;
  flush(echo, store, recorder, { 1 });

  ASSERT_EQ(recorder.messages.size(), 1u);
  ASSERT_EQ(recorder.messages[0].payload.entries.size(), 1u);
  EXPECT_NE(recorder.messages[0].payload.entries[0].chunk, nullptr);
}

TEST(TerrainEditEchoTest, AnErasedChunkGoesOutAsTheAirChunkAndErasesItOnTheView)
{
  terrain::TerrainStore server;
  makeTerrain(server);
  const ChunkCoord coord{ 0, 0, 0 };
  serverSet(server, coord, nullptr);
  ASSERT_EQ(server.find(terrainA)->chunk(coord), nullptr);

  terrain::TerrainEditEcho echo;
  echo.recordEdit(terrainA, std::vector<ChunkCoord>{ coord }, 1);
  Recorder recorder;
  flush(echo, server, recorder, { 2 });

  ASSERT_EQ(recorder.messages.size(), 1u);
  ASSERT_EQ(recorder.messages[0].payload.entries.size(), 1u);
  const auto& entry = recorder.messages[0].payload.entries[0];
  ASSERT_NE(entry.chunk, nullptr);
  EXPECT_EQ(entry.hash, terrain::makeChunkEntry(coord, 1, nullptr).hash);

  terrain::TerrainStore view;
  viewHolds(view, coord, variant(1));
  ASSERT_NE(view.find(terrainA)->chunk(coord), nullptr);
  EXPECT_EQ(terrain::applyTerrainChunks(view, recorder.messages[0].payload).applied, 1u);
  EXPECT_EQ(view.find(terrainA)->chunk(coord), nullptr);
}

TEST(TerrainEditEchoTest, AChunkWithRevisionZeroIsGivenOneDuringTheFlush)
{
  terrain::TerrainStore store;
  makeTerrain(store);
  const ChunkCoord coord{ 3, 0, 0 };
  store.find(terrainA)->setChunk(coord, variant(1));
  ASSERT_EQ(store.revision(terrainA, coord), 0u);

  terrain::TerrainEditEcho echo;
  echo.recordEdit(terrainA, std::vector<ChunkCoord>{ coord }, 1);
  Recorder recorder;
  flush(echo, store, recorder, { 2 });

  ASSERT_EQ(recorder.messages.size(), 1u);
  const uint32_t revision = recorder.messages[0].payload.entries[0].revision;
  EXPECT_GT(revision, 0u);
  EXPECT_EQ(store.revision(terrainA, coord), revision);
}

TEST(TerrainEditEchoTest, TheByteBudgetSplitsAConnectionsEntriesIntoSeveralMessages)
{
  terrain::TerrainStore store;
  makeTerrain(store);
  std::vector<ChunkCoord> coords;

  for (int x = 0; x < 5; ++x)
  {
    coords.push_back({ x, 0, 0 });
    serverSet(store, coords.back(), noise(static_cast<uint32_t>(x)));
  }

  const size_t entryBytes = terrain::chunkEntryHeaderBytes + terrain::makeChunkEntry(coords[0], 1, noise(0)).encoded.size();
  const size_t budget = terrain::terrainChunksHeaderBytes + 2 * entryBytes + 8;

  terrain::TerrainEditEcho echo;
  echo.recordEdit(terrainA, coords, 1);
  Recorder recorder;
  flush(echo, store, recorder, { 2 }, budget);

  ASSERT_EQ(recorder.messages.size(), 3u);
  EXPECT_EQ(recorder.messages[0].payload.entries.size(), 2u);
  EXPECT_EQ(recorder.messages[1].payload.entries.size(), 2u);
  EXPECT_EQ(recorder.messages[2].payload.entries.size(), 1u);

  for (const Received& received : recorder.messages)
  {
    EXPECT_LE(received.bytes, budget);
  }

  // Positive control: a budget that fits them all sends one message.
  echo.recordEdit(terrainA, coords, 1);
  Recorder whole;
  flush(echo, store, whole, { 2 }, 5 * entryBytes + terrain::terrainChunksHeaderBytes);
  EXPECT_EQ(whole.messages.size(), 1u);
}

TEST(TerrainEditEchoTest, AChunkOverTheBudgetIsStillSent)
{
  terrain::TerrainStore store;
  makeTerrain(store);
  std::vector<ChunkCoord> coords{ { 0, 0, 0 }, { 1, 0, 0 } };
  serverSet(store, coords[0], noise(1));
  serverSet(store, coords[1], noise(2));

  terrain::TerrainEditEcho echo;
  echo.recordEdit(terrainA, coords, 1);
  Recorder recorder;
  flush(echo, store, recorder, { 2 }, 1);

  ASSERT_EQ(recorder.messages.size(), 2u);
  EXPECT_EQ(recorder.messages[0].payload.entries.size(), 1u);
  EXPECT_EQ(recorder.messages[1].payload.entries.size(), 1u);
  EXPECT_NE(recorder.messages[0].payload.entries[0].chunk, nullptr);
  EXPECT_NE(recorder.messages[1].payload.entries[0].chunk, nullptr);
}

TEST(TerrainEditEchoTest, MoreThanTheEntryLimitSplitsIntoTwoMessages)
{
  terrain::TerrainStore store;
  makeTerrain(store);
  std::vector<ChunkCoord> coords;

  for (size_t x = 0; x <= terrain::maxTerrainChunksEntries; ++x)
  {
    coords.push_back({ static_cast<int32_t>(x), 0, 0 });
    serverSet(store, coords.back(), solid());
  }

  terrain::TerrainEditEcho echo;
  echo.recordEdit(terrainA, coords, 1);
  Recorder recorder;
  flush(echo, store, recorder, { 2 });

  ASSERT_EQ(recorder.messages.size(), 2u);
  EXPECT_EQ(recorder.messages[0].payload.entries.size(), terrain::maxTerrainChunksEntries);
  EXPECT_EQ(recorder.messages[1].payload.entries.size(), 1u);
}

TEST(TerrainEditEchoTest, AnUnknownTerrainIsDroppedAndNothingPendingSendsNothing)
{
  terrain::TerrainStore store;
  makeTerrain(store);
  const ChunkCoord coord{ 0, 0, 0 };
  serverSet(store, coord, variant(1));
  const std::vector<ChunkCoord> coords{ coord };

  terrain::TerrainEditEcho echo;
  Recorder idle;
  flush(echo, store, idle);
  EXPECT_TRUE(idle.messages.empty());
  EXPECT_FALSE(echo.pending());

  echo.recordEdit(terrainB, coords, 1);
  ASSERT_TRUE(echo.pending());
  Recorder dropped;
  flush(echo, store, dropped);
  EXPECT_TRUE(dropped.messages.empty());
  EXPECT_FALSE(echo.pending());

  // Positive control: a known terrain sends.
  echo.recordEdit(terrainA, coords, 1);
  Recorder known;
  flush(echo, store, known);
  EXPECT_FALSE(known.messages.empty());
}

TEST(TerrainEditEchoTest, OutOfRangeCoordinatesAreIgnoredWhenRecorded)
{
  terrain::TerrainEditEcho echo;
  const std::vector<ChunkCoord> outOfRange{ { terrain::maxChunkCoord + 1, 0, 0 } };
  echo.recordEdit(terrainA, outOfRange, 1);
  echo.requestFull(terrainA, outOfRange, 1);
  EXPECT_FALSE(echo.pending());

  const std::vector<ChunkCoord> inRange{ { terrain::maxChunkCoord, 0, 0 } };
  echo.recordEdit(terrainA, inRange, 1);
  EXPECT_TRUE(echo.pending());
}

TEST(TerrainEditEchoTest, AThrowingSendDoesNotStopOtherConnectionsAndTheRecordIsStillCleared)
{
  terrain::TerrainStore store;
  makeTerrain(store);
  const ChunkCoord coord{ 0, 0, 0 };
  serverSet(store, coord, variant(1));

  terrain::TerrainEditEcho echo;
  echo.recordEdit(terrainA, std::vector<ChunkCoord>{ coord }, 3);
  std::vector<int32_t> served;
  const auto send = [&](const int32_t connId, const net::Message&)
  {
    if (connId == 1)
    {
      throw std::runtime_error("send failed");
    }

    served.push_back(connId);
  };

  const std::vector<int32_t> connections{ 1, 2 };
  EXPECT_THROW(echo.flush(store, connections, send), std::runtime_error);
  ASSERT_EQ(served.size(), 1u);
  EXPECT_EQ(served[0], 2);
  EXPECT_FALSE(echo.pending());

  // Positive control: with nothing throwing the same record serves both connections.
  echo.recordEdit(terrainA, std::vector<ChunkCoord>{ coord }, 3);
  Recorder recorder;
  flush(echo, store, recorder);
  EXPECT_EQ(recorder.messages.size(), 2u);
}

TEST(TerrainEditEchoTest, IsChunkRequestNeedsAtLeastOneEntryAndEveryEntryHashOnly)
{
  const ChunkCoord a{ 0, 0, 0 };
  const ChunkCoord b{ 1, 0, 0 };
  const uint64_t hash = variant(1)->hash();

  terrain::TerrainChunksPayload payload;
  payload.terrain = terrainA;
  EXPECT_FALSE(terrain::isChunkRequest(payload));

  payload.entries.push_back({ a, 1, hash, nullptr });
  payload.entries.push_back({ b, 1, hash, nullptr });
  EXPECT_TRUE(terrain::isChunkRequest(payload));

  payload.entries.push_back({ { 2, 0, 0 }, 1, hash, variant(1) });
  EXPECT_FALSE(terrain::isChunkRequest(payload));

  terrain::TerrainChunksPayload full;
  full.terrain = terrainA;
  full.entries.push_back({ a, 1, hash, variant(1) });
  EXPECT_FALSE(terrain::isChunkRequest(full));
}

TEST(TerrainEditEchoTest, MakeChunkRequestCarriesTheToldRevisionAndHashAndSkipsUnknownCoordinates)
{
  const ChunkCoord a{ 0, 0, 0 };
  const ChunkCoord b{ 1, 0, 0 };
  const ChunkCoord c{ 2, 0, 0 };
  const uint64_t hashA = variant(1)->hash();
  const uint64_t hashB = variant(2)->hash();

  terrain::TerrainChunksPayload received;
  received.terrain = terrainA;
  received.entries.push_back({ a, 4, hashA, nullptr });
  received.entries.push_back({ b, 5, hashB, nullptr });

  const std::vector<ChunkCoord> missing{ b, c };
  const auto request = terrain::unpackTerrainChunks(terrain::makeChunkRequest(received, missing));

  ASSERT_TRUE(request.has_value());
  EXPECT_EQ(request->terrain, terrainA);
  ASSERT_EQ(request->entries.size(), 1u);
  EXPECT_EQ(request->entries[0].coord, b);
  EXPECT_EQ(request->entries[0].revision, 5u);
  EXPECT_EQ(request->entries[0].hash, hashB);
  EXPECT_EQ(request->entries[0].chunk, nullptr);
  EXPECT_TRUE(terrain::isChunkRequest(*request));
}
