#include <gtest/gtest.h>

#include "TerrainBrushSession.h"

#include <ComponentRegistry.h>
#include <scenes/SceneManager.h>
#include <terrain/TerrainBrush.h>
#include <terrain/TerrainChunk.h>
#include <terrain/TerrainEdit.h>
#include <terrain/TerrainEditApply.h>
#include <terrain/TerrainEditEcho.h>
#include <terrain/TerrainGenerators.h>
#include <terrain/TerrainOps.h>
#include <terrain/TerrainReplication.h>
#include <terrain/TerrainStore.h>
#include <terrain/TerrainTypes.h>
#include <terrain/TerrainViewSync.h>
#include <terrain/TerrainVolume.h>
#include <Protocol.h>

#include <glm/vec3.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
#include <uuid.h>

namespace {
  constexpr float voxel = 2.5f;
  constexpr int32_t viewConnection = 1;

  const auto terrainId = uuids::uuid::from_string("11111111-1111-1111-1111-111111111111").value();

  struct Env
  {
    terrain::TerrainStore store;
    std::shared_ptr<terrain::TerrainVolume> volume;

    Env()
    {
      terrain::TerrainDescription description;
      description.uuid = terrainId;
      description.voxelSize = voxel;
      volume = store.create(description);

      const uint32_t revision = store.nextRevision(terrainId);
      for (const auto& [coord, chunk] : terrain::fillFlat(*volume, { 64, 64 }, 10.0f, 20.0f, 1))
      {
        store.setChunk(terrainId, coord, chunk, revision);
      }
    }
  };

  terrain::TerrainVolume::ChunkMap hashes(const terrain::TerrainVolume& volume)
  {
    return volume.snapshotChunks();
  }

  bool sameHashes(const terrain::TerrainVolume::ChunkMap& a, const terrain::TerrainVolume::ChunkMap& b)
  {
    if (a.size() != b.size())
    {
      return false;
    }

    for (const auto& [coord, chunk] : a)
    {
      const auto match = b.find(coord);

      if (match == b.end() || match->second->hash() != chunk->hash())
      {
        return false;
      }
    }

    return true;
  }

  // A server store with the edit echo, and a view store that predicts and follows the echo, joined by a one-way
  // queue each way that the test delivers in chosen amounts.
  struct World
  {
    Env server;
    Env view;
    terrain::TerrainEditEcho echo;
    terrain::TerrainViewSync sync;
    SceneManager scenes;
    TerrainBrushSession session;
    std::vector<net::Message> toServer;
    size_t heldEntries = 0;
    bool useHold = true;

    TerrainBrushSession::Send sender()
    {
      return [this](const net::Message& message) { toServer.push_back(message); };
    }

    void receive(const net::Message& message)
    {
      if (message.getType() == net::MessageType::terrainEdit)
      {
        const auto payload = terrain::unpackTerrainEdit(message);
        ASSERT_TRUE(payload.has_value());
        const terrain::TerrainEditResult result = terrain::applyTerrainEdit(server.store, *payload);
        EXPECT_EQ(result.refusal, terrain::TerrainEditRefusal::none);

        if (!result.changed.empty())
        {
          echo.recordEdit(payload->terrain, result.changed, viewConnection);
        }

        return;
      }

      const auto payload = terrain::unpackTerrainChunks(message);
      ASSERT_TRUE(payload.has_value());
      ASSERT_TRUE(terrain::isChunkRequest(*payload));

      std::vector<terrain::ChunkCoord> coords;
      for (const terrain::ReceivedChunk& entry : payload->entries)
      {
        coords.push_back(entry.coord);
      }

      echo.requestFull(payload->terrain, coords, viewConnection);
    }

    void deliver(const size_t count)
    {
      const size_t take = std::min(count, toServer.size());
      const std::vector<net::Message> batch(toServer.begin(), toServer.begin() + static_cast<std::ptrdiff_t>(take));
      toServer.erase(toServer.begin(), toServer.begin() + static_cast<std::ptrdiff_t>(take));

      for (const net::Message& message : batch)
      {
        receive(message);
      }
    }

    // One echo flush, applied to the view the way the editor applies it; a request the view makes joins the queue.
    void flushToView()
    {
      std::vector<net::Message> echoed;
      const std::vector<int32_t> connections = { viewConnection };
      echo.flush(server.store, connections,
                 [&echoed](const int32_t, const net::Message& message) { echoed.push_back(message); });

      for (const net::Message& message : echoed)
      {
        const terrain::ChunkHold* hold = useHold && session.active() ? &session.touched() : nullptr;
        const terrain::ViewApplyOutcome outcome = sync.apply(view.store, scenes, message, hold);
        heldEntries += outcome.result.held;

        if (outcome.request)
        {
          toServer.push_back(*outcome.request);
        }
      }
    }
  };

  terrain::RayHit hitAt(const glm::vec3 point)
  {
    terrain::RayHit hit;
    hit.point = point;
    hit.normal = { 0.0f, 1.0f, 0.0f };
    return hit;
  }

  struct Outcome
  {
    bool revertedMidStroke = false;
    bool equalAfterRelease = false;
    size_t heldEntries = 0;
  };

  // Delayed delivery: the server is behind the view when the echo reaches it, as it is on every 50 ms flush.
  Outcome runStroke(const terrain::BrushOp::Kind kind, const bool useHold)
  {
    World world;
    world.useHold = useHold;
    Outcome outcome;

    terrain::BrushOp op;
    op.kind = kind;
    op.radius = 10.0f;
    op.strength = 0.8f;
    op.falloff = 0.4f;
    op.material = 1;

    const float pressHeight = kind == terrain::BrushOp::Kind::flatten ? 14.0f : 10.0f;
    const auto send = world.sender();

    EXPECT_TRUE(world.session.begin(terrainId, world.view.volume, op, hitAt({ 40.0f, pressHeight, 40.0f }), send));
    world.session.moveTo(hitAt({ 60.0f, 10.0f, 40.0f }), send);
    world.session.moveTo(hitAt({ 60.0f, 10.0f, 70.0f }), send);

    // The server has seen half of what the view predicted when its first echo goes out.
    world.deliver(world.toServer.size() / 2);
    world.flushToView();

    const size_t beforeMore = world.toServer.size();
    world.session.moveTo(hitAt({ 90.0f, 10.0f, 70.0f }), send);

    // Everything sent before the last moves reaches the server, whose echo (and any answer to a request the view
    // made) is behind the view's newest applications.
    world.deliver(beforeMore);
    const auto before = hashes(*world.view.volume);
    world.flushToView();
    outcome.revertedMidStroke = !sameHashes(before, hashes(*world.view.volume));

    static_cast<void>(world.session.end(send));

    for (int round = 0; round < 4; ++round)
    {
      world.deliver(world.toServer.size());
      world.flushToView();
    }

    outcome.equalAfterRelease = world.toServer.empty() &&
      sameHashes(hashes(*world.view.volume), hashes(*world.server.volume));
    outcome.heldEntries = world.heldEntries;
    return outcome;
  }
}

class TerrainStrokeEchoTest : public ::testing::TestWithParam<terrain::BrushOp::Kind> {};

TEST_P(TerrainStrokeEchoTest, HoldingTheTouchedChunksKeepsThePredictionUntilTheFinalStateArrives)
{
  const Outcome outcome = runStroke(GetParam(), true);

  EXPECT_FALSE(outcome.revertedMidStroke);
  EXPECT_GT(outcome.heldEntries, 0u);
  EXPECT_TRUE(outcome.equalAfterRelease);
}

TEST_P(TerrainStrokeEchoTest, WithoutTheHoldTheEchoRevertsAPredictedChunk)
{
  const Outcome outcome = runStroke(GetParam(), false);

  EXPECT_TRUE(outcome.revertedMidStroke);
  EXPECT_EQ(outcome.heldEntries, 0u);
}

INSTANTIATE_TEST_SUITE_P(Kinds, TerrainStrokeEchoTest,
                         ::testing::Values(terrain::BrushOp::Kind::add, terrain::BrushOp::Kind::smooth,
                                           terrain::BrushOp::Kind::flatten));

TEST(TerrainChunkHoldTest, TheSessionHoldsItsTouchedChunksOnlyForItsTerrainWhileActive)
{
  Env env;
  TerrainBrushSession session;
  const auto otherTerrain = uuids::uuid::from_string("22222222-2222-2222-2222-222222222222").value();
  terrain::BrushOp op;
  op.radius = 10.0f;
  const auto send = [](const net::Message&) {};

  EXPECT_EQ(session.holdFor(terrainId), nullptr);

  ASSERT_TRUE(session.begin(terrainId, env.volume, op, hitAt({ 40.0f, 10.0f, 40.0f }), send));
  ASSERT_NE(session.holdFor(terrainId), nullptr);
  EXPECT_FALSE(session.holdFor(terrainId)->empty());
  EXPECT_EQ(session.holdFor(otherTerrain), nullptr);

  static_cast<void>(session.end(send));
  EXPECT_EQ(session.holdFor(terrainId), nullptr);
}

TEST(TerrainChunkHoldTest, AHeldCoordinateIsSkippedWhateverTheEntryCarriesAndAnotherIsApplied)
{
  Env env;
  const terrain::ChunkCoord held = terrain::chunkOfSample({ 16, 4, 16 });
  const terrain::ChunkCoord other = terrain::chunkOfSample({ 48, 4, 16 });
  const auto original = env.volume->chunk(held);
  const auto otherOriginal = env.volume->chunk(other);
  ASSERT_TRUE(original && otherOriginal);

  const auto changed = terrain::TerrainChunk::uniform(static_cast<int16_t>(-100), 3);
  ASSERT_NE(changed->hash(), original->hash());
  ASSERT_NE(changed->hash(), otherOriginal->hash());

  const std::vector<terrain::ChunkEntry> entries = {
    terrain::makeChunkEntry(held, 9, changed),
    terrain::makeChunkEntry(other, 9, changed)
  };
  const auto payload = terrain::unpackTerrainChunks(terrain::packTerrainChunks(terrainId, entries));
  ASSERT_TRUE(payload.has_value());

  const terrain::ChunkHold hold = { held };
  const terrain::ChunksApplyResult result = terrain::applyTerrainChunks(env.store, *payload, &hold);

  EXPECT_EQ(result.held, 1u);
  EXPECT_EQ(result.applied, 1u);
  EXPECT_EQ(env.volume->chunk(held), original);
  EXPECT_EQ(env.store.revision(terrainId, held), 1u);
  EXPECT_EQ(env.volume->chunk(other)->hash(), changed->hash());
  EXPECT_EQ(env.store.revision(terrainId, other), 9u);
}

TEST(TerrainChunkHoldTest, AHeldHashOnlyMismatchIsNotRequestedButAnUnheldOneIs)
{
  Env env;
  SceneManager scenes;
  terrain::TerrainViewSync sync;
  const terrain::ChunkCoord coord = terrain::chunkOfSample({ 16, 4, 16 });

  const std::vector<terrain::ChunkEntry> entries = { terrain::makeHashOnlyEntry(coord, 9, 12345) };
  const net::Message message = terrain::packTerrainChunks(terrainId, entries);

  const terrain::ChunkHold hold = { coord };
  const terrain::ViewApplyOutcome held = sync.apply(env.store, scenes, message, &hold);
  EXPECT_EQ(held.result.held, 1u);
  EXPECT_FALSE(held.request.has_value());

  const terrain::ViewApplyOutcome open = sync.apply(env.store, scenes, message);
  EXPECT_EQ(open.result.missing.size(), 1u);
  EXPECT_TRUE(open.request.has_value());
}
