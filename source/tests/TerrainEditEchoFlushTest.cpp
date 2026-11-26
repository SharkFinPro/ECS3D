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

#include "TerrainEditEchoFixtures.h"

using namespace terrainEchoFixtures;

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
