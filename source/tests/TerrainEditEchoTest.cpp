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

TEST(TerrainEditEchoTest, AChunkTwoEditorsChangedStaysFullForBothAfterOneDisconnects)
{
  terrain::TerrainStore store;
  makeTerrain(store);
  const ChunkCoord coord{ 0, 0, 0 };
  serverSet(store, coord, variant(1));

  terrain::TerrainEditEcho echo;
  const std::vector<ChunkCoord> coords{ coord };
  echo.recordEdit(terrainA, coords, 1);
  echo.recordEdit(terrainA, coords, 2);
  echo.forget(2);

  Recorder recorder;
  flush(echo, store, recorder, { 1, 3 });

  for (const int32_t connId : { 1, 3 })
  {
    const auto got = recorder.from(connId);
    ASSERT_EQ(got.size(), 1u);
    ASSERT_EQ(got[0].payload.entries.size(), 1u);
    EXPECT_NE(got[0].payload.entries[0].chunk, nullptr);
  }

  // Positive control: without the second editor the first still gets hash-only.
  serverSet(store, coord, variant(2));
  echo.recordEdit(terrainA, coords, 1);
  Recorder alone;
  flush(echo, store, alone, { 1, 3 });
  ASSERT_EQ(alone.from(1).size(), 1u);
  EXPECT_EQ(alone.from(1)[0].payload.entries[0].chunk, nullptr);
}

TEST(TerrainEditEchoTest, TheByteBudgetBoundaryIsInclusive)
{
  terrain::TerrainStore store;
  makeTerrain(store);
  const std::vector<ChunkCoord> coords{ { 0, 0, 0 }, { 1, 0, 0 } };
  serverSet(store, coords[0], noise(1));
  serverSet(store, coords[1], noise(2));

  const size_t exact = terrain::terrainChunksHeaderBytes +
                       2 * terrain::chunkEntryHeaderBytes +
                       terrain::makeChunkEntry(coords[0], 1, noise(1)).encoded.size() +
                       terrain::makeChunkEntry(coords[1], 1, noise(2)).encoded.size();

  terrain::TerrainEditEcho echo;
  echo.recordEdit(terrainA, coords, 1);
  Recorder fits;
  flush(echo, store, fits, { 2 }, exact);
  ASSERT_EQ(fits.messages.size(), 1u);
  EXPECT_EQ(fits.messages[0].payload.entries.size(), 2u);

  echo.recordEdit(terrainA, coords, 1);
  Recorder splits;
  flush(echo, store, splits, { 2 }, exact - 1);
  ASSERT_EQ(splits.messages.size(), 2u);
  EXPECT_EQ(splits.messages[0].payload.entries.size(), 1u);
  EXPECT_EQ(splits.messages[1].payload.entries.size(), 1u);
}

TEST(TerrainEditEchoTest, ClearDropsEditsAndRequestsSoNothingIsSent)
{
  terrain::TerrainStore store;
  makeTerrain(store);
  const std::vector<ChunkCoord> edited{ { 0, 0, 0 } };
  const std::vector<ChunkCoord> requested{ { 1, 0, 0 } };
  serverSet(store, edited[0], noise(1));
  serverSet(store, requested[0], noise(2));

  terrain::TerrainEditEcho echo;
  echo.recordEdit(terrainA, edited, 1);
  echo.requestFull(terrainA, requested, 2);
  ASSERT_TRUE(echo.pending());

  // Positive control: what clear drops is otherwise sent.
  terrain::TerrainEditEcho control = echo;
  Recorder sent;
  flush(control, store, sent);
  EXPECT_EQ(chunkTotal(sent.messages), 3u);

  echo.clear();

  EXPECT_FALSE(echo.pending());
  Recorder recorder;
  flush(echo, store, recorder);
  EXPECT_TRUE(recorder.messages.empty());

  // The echo records again after a clear.
  echo.recordEdit(terrainA, edited, 1);
  EXPECT_TRUE(echo.pending());
}
