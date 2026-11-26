#include <gtest/gtest.h>

#include "scenes/SceneManager.h"
#include "terrain/TerrainBrush.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainEdit.h"
#include "terrain/TerrainEditApply.h"
#include "terrain/TerrainEditEcho.h"
#include "terrain/TerrainGenerators.h"
#include "terrain/TerrainOps.h"
#include "terrain/TerrainReplication.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainViewSync.h"
#include "terrain/TerrainVolume.h"

#include <Protocol.h>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <unordered_set>
#include <utility>
#include <vector>

// The server's store and two views wired together the way ServerApp and the editor wire them, with no sockets: an
// edit is packed, parsed, applied and recorded by the server side, and the echo is flushed into the views.
namespace {
  using terrain::ChunkCoord;

  constexpr int32_t connA = 1;
  constexpr int32_t connB = 2;

  const auto terrainId = uuids::uuid::from_string("11111111-1111-1111-1111-111111111111").value();

  uint64_t airHash()
  {
    static const auto air = terrain::TerrainChunk::uniform(terrain::airDistance, 0);
    return air->hash();
  }

  uint64_t hashIn(const terrain::TerrainVolume::ChunkMap& chunks, const ChunkCoord coord)
  {
    const auto found = chunks.find(coord);
    return found == chunks.end() || !found->second ? airHash() : found->second->hash();
  }

  terrain::BrushOp addBrush(const glm::vec3 center, const float radius)
  {
    terrain::BrushOp op;
    op.kind = terrain::BrushOp::Kind::add;
    op.center = center;
    op.radius = radius;
    op.material = 7;
    return op;
  }

  struct Node
  {
    terrain::TerrainStore store;
    std::shared_ptr<terrain::TerrainVolume> volume;
    terrain::TerrainViewSync sync;

    Node()
    {
      terrain::TerrainDescription description;
      description.uuid = terrainId;
      description.voxelSize = 2.5f;
      volume = store.create(description);
    }

    [[nodiscard]] uint64_t hashAt(const ChunkCoord coord) const
    {
      return hashIn(volume->chunks(), coord);
    }
  };

  struct Flow
  {
    Node server;
    Node a;
    Node b;
    SceneManager scenes;
    terrain::TerrainEditEcho echo;

    std::vector<std::pair<int32_t, net::Message>> requests;
    std::map<int32_t, size_t> fullEntries;
    std::map<int32_t, size_t> hashOnlyEntries;
    std::map<int32_t, size_t> messages;
    std::map<int32_t, size_t> bytes;

    Flow()
    {
      const uint32_t revision = server.store.nextRevision(terrainId);
      for (const auto& [coord, chunk] : terrain::fillFlat(*server.volume, { 64, 64 }, 10.0f, 20.0f, 1))
      {
        server.store.setChunk(terrainId, coord, chunk, revision);
      }

      syncWholeTerrain(a);
      syncWholeTerrain(b);
    }

    // What a joining connection's stream delivers.
    void syncWholeTerrain(Node& view)
    {
      std::vector<terrain::ChunkEntry> entries;
      for (const auto& [coord, chunk] : server.volume->chunks())
      {
        entries.push_back(terrain::makeChunkEntry(coord, server.store.revision(terrainId, coord), chunk));
      }

      ASSERT_FALSE(entries.empty());
      ASSERT_LE(entries.size(), terrain::maxTerrainChunksEntries);

      const auto payload = terrain::unpackTerrainChunks(terrain::packTerrainChunks(terrainId, entries));
      ASSERT_TRUE(payload.has_value());
      EXPECT_EQ(terrain::applyTerrainChunks(view.store, *payload).applied, entries.size());
    }

    // The editor's local prediction of a brush: the same pure op the server runs, with no revision.
    static void predict(Node& view, const terrain::BrushOp& op)
    {
      terrain::commit(*view.volume, terrain::applyBrush(*view.volume, op));
    }

    // ServerApp::handleTerrainEdit. Nullopt for a malformed message.
    std::optional<terrain::TerrainEditResult> serverEdit(const net::Message& message, const int32_t connId)
    {
      const auto edit = terrain::unpackTerrainEdit(message);
      if (!edit)
      {
        return std::nullopt;
      }

      auto result = terrain::applyTerrainEdit(server.store, *edit);
      if (result.refusal == terrain::TerrainEditRefusal::none && !result.changed.empty())
      {
        echo.recordEdit(edit->terrain, result.changed, connId);
      }

      return result;
    }

    std::optional<terrain::TerrainEditResult> serverBrush(const terrain::BrushOp& op, const int32_t connId)
    {
      return serverEdit(terrain::packBrushEdit(terrainId, 1, op), connId);
    }

    // The request branch of ServerApp::handleTerrainUpload.
    void serverRequest(const net::Message& message, const int32_t connId)
    {
      const auto payload = terrain::unpackTerrainChunks(message);
      ASSERT_TRUE(payload.has_value());
      ASSERT_TRUE(terrain::isChunkRequest(*payload));

      std::vector<ChunkCoord> coords;
      for (const auto& entry : payload->entries)
      {
        coords.push_back(entry.coord);
      }

      echo.requestFull(payload->terrain, coords, connId);
    }

    void routeRequests()
    {
      auto pending = std::move(requests);
      requests.clear();

      for (const auto& [connId, message] : pending)
      {
        serverRequest(message, connId);
      }
    }

    // ServerApp::flushTerrainEcho, into the views the way the editor applies terrainChunks.
    void deliver()
    {
      const std::vector<int32_t> connections{ connA, connB };

      echo.flush(server.store, connections, [this](const int32_t connId, const net::Message& message)
      {
        const auto payload = terrain::unpackTerrainChunks(message);
        ASSERT_TRUE(payload.has_value());

        for (const auto& entry : payload->entries)
        {
          ++(entry.chunk ? fullEntries : hashOnlyEntries)[connId];
        }

        ++messages[connId];
        bytes[connId] += message.size();

        Node& view = connId == connA ? a : b;
        auto outcome = view.sync.apply(view.store, scenes, message);
        if (outcome.request)
        {
          requests.emplace_back(connId, std::move(*outcome.request));
        }
      });
    }

    void expectConverged() const
    {
      std::vector<ChunkCoord> coords;
      for (const Node* node : { &server, &a, &b })
      {
        for (const auto& entry : node->volume->chunks())
        {
          coords.push_back(entry.first);
        }
      }

      ASSERT_FALSE(coords.empty());

      for (const ChunkCoord coord : coords)
      {
        EXPECT_EQ(a.hashAt(coord), server.hashAt(coord)) << coord.x << "," << coord.y << "," << coord.z;
        EXPECT_EQ(b.hashAt(coord), server.hashAt(coord)) << coord.x << "," << coord.y << "," << coord.z;
      }
    }
  };

  bool contains(const std::vector<ChunkCoord>& coords, const ChunkCoord coord)
  {
    return std::ranges::find(coords, coord) != coords.end();
  }

  bool overlap(const std::vector<ChunkCoord>& first, const std::vector<ChunkCoord>& second)
  {
    return std::ranges::any_of(first, [&second](const ChunkCoord coord) { return contains(second, coord); });
  }

  const terrain::BrushOp bigBrushA = addBrush({ 80.0f, 10.0f, 80.0f }, 10.0f);
  const terrain::BrushOp bigBrushB = addBrush({ 70.0f, 10.0f, 90.0f }, 10.0f);
}

TEST(TerrainEditFlow, ACorrectPredictionCostsTheSenderNoChunkBytesAndEveryoneConverges)
{
  Flow flow;
  const auto beforeB = flow.b.volume->snapshotChunks();
  const auto beforeA = flow.a.volume->snapshotChunks();

  flow.predict(flow.a, bigBrushA);
  const auto result = flow.serverBrush(bigBrushA, connA);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->refusal, terrain::TerrainEditRefusal::none);
  ASSERT_FALSE(result->changed.empty());
  ASSERT_TRUE(flow.echo.pending());

  flow.deliver();

  EXPECT_FALSE(flow.echo.pending());
  EXPECT_TRUE(flow.requests.empty());
  EXPECT_EQ(flow.fullEntries[connA], 0u);
  EXPECT_EQ(flow.hashOnlyEntries[connA], result->changed.size());
  EXPECT_EQ(flow.hashOnlyEntries[connB], 0u);
  EXPECT_EQ(flow.fullEntries[connB], result->changed.size());
  EXPECT_LT(flow.bytes[connA], flow.bytes[connB]);

  for (const ChunkCoord coord : result->changed)
  {
    EXPECT_NE(flow.b.hashAt(coord), hashIn(beforeB, coord));
    EXPECT_NE(flow.a.hashAt(coord), hashIn(beforeA, coord));
  }

  flow.expectConverged();
}

TEST(TerrainEditFlow, AViewThatDidNotPredictAsksForTheChunksAndTheServerRepairsOnlyIt)
{
  Flow flow;
  const auto result = flow.serverBrush(bigBrushA, connA);
  ASSERT_TRUE(result.has_value());
  ASSERT_FALSE(result->changed.empty());

  flow.deliver();

  ASSERT_EQ(flow.requests.size(), 1u);
  EXPECT_EQ(flow.requests[0].first, connA);
  EXPECT_EQ(flow.hashOnlyEntries[connA], result->changed.size());
  EXPECT_NE(flow.a.hashAt(result->changed[0]), flow.server.hashAt(result->changed[0]));

  flow.routeRequests();
  ASSERT_TRUE(flow.echo.pending());

  const size_t fullToB = flow.fullEntries[connB];
  flow.deliver();

  EXPECT_TRUE(flow.requests.empty());
  EXPECT_EQ(flow.fullEntries[connA], result->changed.size());
  EXPECT_EQ(flow.fullEntries[connB], fullToB);
  flow.expectConverged();
}

TEST(TerrainEditFlow, AWrongPredictionIsRepairedTheSameWay)
{
  Flow flow;
  flow.predict(flow.a, addBrush({ 80.0f, 10.0f, 80.0f }, 4.0f));

  const auto result = flow.serverBrush(bigBrushA, connA);
  ASSERT_TRUE(result.has_value());
  ASSERT_FALSE(result->changed.empty());

  flow.deliver();

  ASSERT_EQ(flow.requests.size(), 1u);
  flow.routeRequests();
  flow.deliver();

  EXPECT_TRUE(flow.requests.empty());
  EXPECT_GT(flow.fullEntries[connA], 0u);
  flow.expectConverged();
}

TEST(TerrainEditFlow, TwoStrokesOnOverlappingChunksInOneIntervalGoToBothInFull)
{
  Flow flow;
  flow.predict(flow.a, bigBrushA);
  flow.predict(flow.b, bigBrushB);

  const auto first = flow.serverBrush(bigBrushA, connA);
  const auto second = flow.serverBrush(bigBrushB, connB);
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  ASSERT_TRUE(overlap(first->changed, second->changed));

  flow.deliver();

  // Chunks both strokes changed went out in full to both views and the rest hash-only to their one sender, so nobody
  // needed a request.
  EXPECT_TRUE(flow.requests.empty());
  EXPECT_GT(flow.fullEntries[connA], 0u);
  EXPECT_GT(flow.fullEntries[connB], 0u);
  flow.expectConverged();
}

TEST(TerrainEditFlow, StrokesAcrossIntervalsConverge)
{
  Flow flow;
  flow.predict(flow.a, bigBrushA);
  ASSERT_TRUE(flow.serverBrush(bigBrushA, connA).has_value());
  flow.deliver();
  flow.expectConverged();

  const auto later = addBrush({ 60.0f, 10.0f, 60.0f }, 8.0f);
  flow.predict(flow.b, later);
  const auto result = flow.serverBrush(later, connB);
  ASSERT_TRUE(result.has_value());
  ASSERT_FALSE(result->changed.empty());
  flow.deliver();

  const auto last = addBrush({ 100.0f, 10.0f, 100.0f }, 8.0f);
  flow.predict(flow.a, last);
  ASSERT_TRUE(flow.serverBrush(last, connA).has_value());
  flow.deliver();

  EXPECT_TRUE(flow.requests.empty());
  flow.expectConverged();
}

TEST(TerrainEditFlow, TenEditsToOneChunkInOneIntervalGoOutAsOneEntryPerView)
{
  Flow flow;
  size_t recorded = 0;
  std::unordered_set<ChunkCoord> distinct;

  for (int i = 0; i < 10; ++i)
  {
    const auto op = addBrush({ 30.0f + 2.0f * static_cast<float>(i), 10.0f, 40.0f }, 5.0f);
    flow.predict(flow.a, op);

    const auto result = flow.serverBrush(op, connA);
    ASSERT_TRUE(result.has_value());
    recorded += result->changed.size();
    distinct.insert(result->changed.begin(), result->changed.end());
  }

  ASSERT_FALSE(distinct.empty());
  EXPECT_GT(recorded, distinct.size());

  flow.deliver();

  EXPECT_EQ(flow.messages[connA], 1u);
  EXPECT_EQ(flow.messages[connB], 1u);
  EXPECT_EQ(flow.hashOnlyEntries[connA], distinct.size());
  EXPECT_EQ(flow.fullEntries[connB], distinct.size());
  EXPECT_TRUE(flow.requests.empty());
  flow.expectConverged();
}

TEST(TerrainEditFlow, MalformedAndRefusedEditsLeaveEveryStoreUnchanged)
{
  Flow flow;
  const auto serverBefore = flow.server.volume->snapshotChunks();
  const uint32_t counterBefore = flow.server.store.nextRevision(terrainId);

  const auto good = terrain::packBrushEdit(terrainId, 1, bigBrushA);
  const auto goodBytes = good.bytes();
  const net::Message truncated(net::MessageType::terrainEdit, std::span<const uint8_t>(goodBytes).first(10));

  EXPECT_TRUE(terrain::unpackTerrainEdit(good).has_value());
  EXPECT_FALSE(flow.serverEdit(truncated, connA).has_value());

  const auto tooBig = flow.serverBrush(addBrush({ 80.0f, 10.0f, 80.0f }, 400.0f), connA);
  ASSERT_TRUE(tooBig.has_value());
  EXPECT_EQ(tooBig->refusal, terrain::TerrainEditRefusal::invalidOp);
  EXPECT_TRUE(tooBig->changed.empty());

  const auto unknown = flow.serverEdit(terrain::packBrushEdit(uuids::uuid::from_string(
    "22222222-2222-2222-2222-222222222222").value(), 1, bigBrushA), connA);
  ASSERT_TRUE(unknown.has_value());
  EXPECT_EQ(unknown->refusal, terrain::TerrainEditRefusal::unknownTerrain);

  EXPECT_FALSE(flow.echo.pending());
  EXPECT_EQ(flow.server.volume->snapshotChunks(), serverBefore);
  EXPECT_EQ(flow.server.store.nextRevision(terrainId), counterBefore + 1);
  flow.expectConverged();

  // The same path with a valid edit does change the server and records it.
  const auto valid = flow.serverBrush(bigBrushA, connA);
  ASSERT_TRUE(valid.has_value());
  EXPECT_FALSE(valid->changed.empty());
  EXPECT_TRUE(flow.echo.pending());
  EXPECT_NE(flow.server.volume->snapshotChunks(), serverBefore);
}
