#include <gtest/gtest.h>

#include "TerrainBrushSession.h"

#include <scenes/SceneManager.h>
#include <terrain/TerrainBrush.h>
#include <terrain/TerrainChunk.h>
#include <terrain/TerrainEdit.h>
#include <terrain/TerrainEditApply.h>
#include <terrain/TerrainEditEcho.h>
#include <terrain/TerrainGenerators.h>
#include <terrain/TerrainRaycast.h>
#include <terrain/TerrainReplication.h>
#include <terrain/TerrainStore.h>
#include <terrain/TerrainTypes.h>
#include <terrain/TerrainViewSync.h>
#include <terrain/TerrainVolume.h>
#include <Protocol.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
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

    explicit Env(const float ground = 10.0f, const int size = 52)
    {
      terrain::TerrainDescription description;
      description.uuid = terrainId;
      description.voxelSize = voxel;
      volume = store.create(description);

      const uint32_t revision = store.nextRevision(terrainId);
      for (const auto& [coord, chunk] : terrain::fillFlat(*volume, { size, size }, ground, 20.0f, 1))
      {
        store.setChunk(terrainId, coord, chunk, revision);
      }
    }
  };

  // The coordinates whose chunk differs between the two volumes (a missing chunk is air).
  std::vector<terrain::ChunkCoord> mismatches(const terrain::TerrainVolume& a, const terrain::TerrainVolume& b)
  {
    std::vector<terrain::ChunkCoord> differing;
    const auto air = terrain::TerrainChunk::uniform(terrain::airDistance, 0);

    const auto check = [&](const terrain::TerrainVolume& from, const terrain::TerrainVolume& other)
    {
      for (const auto& [coord, chunk] : from.chunks())
      {
        const auto match = other.chunk(coord);

        if ((match ? match->hash() : air->hash()) != chunk->hash() &&
            std::ranges::find(differing, coord) == differing.end())
        {
          differing.push_back(coord);
        }
      }
    };

    check(a, b);
    check(b, a);
    return differing;
  }

  struct World
  {
    Env server;
    Env view;

    World(const float ground, const int size) : server(ground, size), view(ground, size) {}

    terrain::TerrainEditEcho echo;
    terrain::TerrainViewSync sync;
    SceneManager scenes;
    TerrainBrushSession session;
    std::vector<net::Message> toServer;
    size_t missing = 0;

    TerrainBrushSession::Send sender()
    {
      return [this](const net::Message& message) { toServer.push_back(message); };
    }

    // Applies up to count queued messages on the server; a chunk request is registered with the echo.
    void deliver(const size_t count)
    {
      size_t taken = 0;

      while (taken < count && !toServer.empty())
      {
        const net::Message message = toServer.front();
        toServer.erase(toServer.begin());
        ++taken;

        const auto payload = terrain::unpackTerrainEdit(message);

        if (!payload)
        {
          const auto request = terrain::unpackTerrainChunks(message);
          ASSERT_TRUE(request.has_value());
          std::vector<terrain::ChunkCoord> coords;

          for (const terrain::ReceivedChunk& entry : request->entries)
          {
            coords.push_back(entry.coord);
          }

          echo.requestFull(request->terrain, coords, viewConnection);
          continue;
        }

        const terrain::TerrainEditResult result = terrain::applyTerrainEdit(server.store, *payload);
        EXPECT_EQ(result.refusal, terrain::TerrainEditRefusal::none);

        if (!result.changed.empty())
        {
          echo.recordEdit(payload->terrain, result.changed, viewConnection);
        }
      }
    }

    void flushToView()
    {
      std::vector<net::Message> echoed;
      const std::vector<int32_t> connections = { viewConnection };
      echo.flush(server.store, connections,
                 [&echoed](const int32_t, const net::Message& message) { echoed.push_back(message); });

      for (const net::Message& message : echoed)
      {
        const auto payload = terrain::unpackTerrainChunks(message);
        ASSERT_TRUE(payload.has_value());
        session.confirmChunks(*payload);
        const terrain::ViewApplyOutcome outcome = sync.apply(view.store, scenes, message, session.holdFor(terrainId));
        missing += outcome.result.missing.size();
      }
    }
  };

  terrain::RayHit surfaceAt(const terrain::TerrainVolume& volume, const float x, const float z)
  {
    const auto hit = terrain::raycast(volume, { x, 300.0f, z }, { 0.0f, -1.0f, 0.0f }, 400.0f);
    EXPECT_TRUE(hit.has_value());
    return hit.value_or(terrain::RayHit{});
  }

  struct Options
  {
    size_t deliverEvery = 0;
    size_t flushEvery = 0;
    float ground = 10.0f;
    int size = 52;
    float span = 1.0f;
    // After release, before the rest reaches the server: how many messages reach it, or npos for none of this step.
    size_t afterRelease = std::string::npos;
  };

  struct Outcome
  {
    // Chunks whose prediction differs from the server once it applied everything, before any answer.
    size_t mismatched = 0;
    // Hash-only echo entries the view could not match, each a needless request for a chunk it predicted.
    size_t unmatchedEchoes = 0;
    bool converged = false;
    bool stillAwaiting = true;
  };

  // A hill stroke: the add brush rides the raised surface, each center a fresh raycast onto the view volume.
  Outcome runHill(const Options options)
  {
    World world(options.ground, options.size);
    const auto send = world.sender();

    terrain::BrushOp op;
    op.kind = terrain::BrushOp::Kind::add;
    op.radius = 20.0f;
    op.strength = 0.12f;
    op.falloff = 0.5f;
    op.material = 1;

    const std::vector<glm::vec2> path = { { 40.0f, 50.0f }, { 60.0f, 50.0f }, { 80.0f, 50.0f }, { 80.0f, 70.0f },
                                          { 60.0f, 70.0f }, { 40.0f, 70.0f }, { 40.0f, 60.0f }, { 60.0f, 60.0f } };
    size_t moves = 0;

    EXPECT_TRUE(world.session.begin(terrainId, world.view.volume, op,
                                    surfaceAt(*world.view.volume, path[0].x, path[0].y), send));

    for (size_t i = 1; i < path.size(); ++i)
    {
      for (int step = 1; step <= 4; ++step)
      {
        const glm::vec2 at = path[i - 1] + (path[i] - path[i - 1]) * (static_cast<float>(step) / 4.0f);
        world.session.moveTo(surfaceAt(*world.view.volume, 20.0f + (at.x - 20.0f) * options.span, at.y), send);
        ++moves;

        if (options.deliverEvery > 0 && moves % options.deliverEvery == 0)
        {
          world.deliver(world.toServer.size());
        }

        if (options.flushEvery > 0 && moves % options.flushEvery == 0)
        {
          world.flushToView();
        }
      }
    }

    EXPECT_TRUE(world.session.end(send).has_value());

    if (options.afterRelease != std::string::npos)
    {
      world.deliver(options.afterRelease);
      world.flushToView();
    }

    world.deliver(world.toServer.size());

    Outcome outcome;
    outcome.mismatched = mismatches(*world.view.volume, *world.server.volume).size();
    outcome.unmatchedEchoes = world.missing;

    for (int round = 0; round < 4; ++round)
    {
      world.flushToView();
      world.deliver(world.toServer.size());
    }

    outcome.converged = world.toServer.empty() && mismatches(*world.view.volume, *world.server.volume).empty();
    outcome.stillAwaiting = world.session.awaitingFinalChunks();
    return outcome;
  }
}

TEST(TerrainPredictionTest, TheServerReplayingTheStrokeMatchesThePredictionExactly)
{
  for (float ground = 5.0f; ground < 170.0f; ground += 12.5f)
  {
    const Outcome outcome = runHill({ 0, 0, ground });

    EXPECT_EQ(outcome.mismatched, 0u) << ground;
    EXPECT_TRUE(outcome.converged) << ground;
  }
}

TEST(TerrainPredictionTest, TheHeldEchoDeliveredMidStrokeKeepsThePredictionExact)
{
  for (const Options options : { Options{ 3, 3 }, Options{ 1, 2 }, Options{ 5, 4 }, Options{ 1, 1 } })
  {
    const Outcome outcome = runHill(options);

    EXPECT_EQ(outcome.mismatched, 0u);
    EXPECT_EQ(outcome.unmatchedEchoes, 0u);
    EXPECT_TRUE(outcome.converged);
    EXPECT_FALSE(outcome.stillAwaiting);
  }
}

TEST(TerrainPredictionTest, AnEchoFlushedAfterTheReleaseBeforeTheRedistanceDoesNotFlagThePredictedChunks)
{
  // Every brush reached the server, then the echo went out before its redistance did.
  for (const size_t delivered : { size_t{ 0 }, size_t{ 1 }, size_t{ 2 } })
  {
    Options options;
    options.deliverEvery = 1;
    options.afterRelease = delivered;
    const Outcome outcome = runHill(options);

    EXPECT_EQ(outcome.mismatched, 0u) << delivered;
    EXPECT_EQ(outcome.unmatchedEchoes, 0u) << delivered;
    EXPECT_TRUE(outcome.converged) << delivered;
    EXPECT_FALSE(outcome.stillAwaiting) << delivered;
  }
}

TEST(TerrainPredictionTest, AnEchoBetweenTwoRedistancePiecesDoesNotFlagAChunkOnlyTheRedistanceChanged)
{
  // A long stroke redistances in two pieces; the surface at 85 m puts chunks the brush never changed in reach of the
  // redistance. The server has applied the first piece when the echo goes out.
  Options options;
  options.deliverEvery = 1;
  options.ground = 85.0f;
  options.size = 220;
  options.span = 8.0f;
  options.afterRelease = 1;

  const Outcome outcome = runHill(options);

  EXPECT_EQ(outcome.mismatched, 0u);
  EXPECT_EQ(outcome.unmatchedEchoes, 0u);
  EXPECT_TRUE(outcome.converged);
  EXPECT_FALSE(outcome.stillAwaiting);
}
