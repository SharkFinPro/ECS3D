#ifndef TERRAINBRUSHSESSIONTESTHELPERS_H
#define TERRAINBRUSHSESSIONTESTHELPERS_H

#include "TerrainBrushSession.h"

#include <gtest/gtest.h>
#include <terrain/TerrainBrush.h>
#include <terrain/TerrainChunk.h>
#include <terrain/TerrainEdit.h>
#include <terrain/TerrainEditApply.h>
#include <terrain/TerrainGenerators.h>
#include <terrain/TerrainRaycast.h>
#include <terrain/TerrainStore.h>
#include <terrain/TerrainTypes.h>
#include <terrain/TerrainVolume.h>
#include <Protocol.h>

#include <glm/vec3.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
#include <uuid.h>

namespace brushSessionTest
{
  inline constexpr float voxel = 2.5f;

  inline const auto terrainId = uuids::uuid::from_string("11111111-1111-1111-1111-111111111111").value();

  inline terrain::RayHit hitAt(const glm::vec3 point)
  {
    terrain::RayHit hit;
    hit.point = point;
    hit.normal = { 0.0f, 1.0f, 0.0f };
    return hit;
  }

  inline terrain::BrushOp templateOp(const terrain::BrushOp::Kind kind, const float radius = 10.0f)
  {
    terrain::BrushOp op;
    op.kind = kind;
    op.radius = radius;
    op.strength = 0.8f;
    op.falloff = 0.4f;
    op.material = 1;
    return op;
  }

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

  struct Recorder
  {
    std::vector<net::Message> sent;

    TerrainBrushSession::Send sender()
    {
      return [this](const net::Message& message) { sent.push_back(message); };
    }

    [[nodiscard]] std::vector<terrain::TerrainEditPayload> payloads() const
    {
      std::vector<terrain::TerrainEditPayload> decoded;

      for (const net::Message& message : sent)
      {
        if (message.getType() != net::MessageType::terrainEdit)
        {
          continue;
        }

        const auto payload = terrain::unpackTerrainEdit(message);
        EXPECT_TRUE(payload.has_value());

        if (payload)
        {
          decoded.push_back(*payload);
        }
      }

      return decoded;
    }
  };

  // The stroke a pointer would draw: press, then a bent drag across the flat ground. A flatten captures its plane at
  // the press, so it starts above the ground to have something to cut.
  inline void drag(TerrainBrushSession& session, Env& env, const terrain::BrushOp& op, Recorder& recorder)
  {
    const float pressHeight = op.kind == terrain::BrushOp::Kind::flatten ? 14.0f : 10.0f;

    ASSERT_TRUE(session.begin(terrainId, env.volume, op, hitAt({ 40.0f, pressHeight, 40.0f }), recorder.sender()));
    session.moveTo(hitAt({ 60.0f, 10.0f, 40.0f }), recorder.sender());
    session.moveTo(hitAt({ 60.0f, 10.0f, 70.0f }), recorder.sender());
  }

  inline void applyToServer(Env& server, const std::vector<net::Message>& messages)
  {
    for (const net::Message& message : messages)
    {
      if (message.getType() != net::MessageType::terrainEdit)
      {
        continue;
      }

      const auto payload = terrain::unpackTerrainEdit(message);
      ASSERT_TRUE(payload.has_value());
      EXPECT_EQ(terrain::applyTerrainEdit(server.store, *payload).refusal, terrain::TerrainEditRefusal::none);
    }
  }

  // Equal when every chunk of either volume exists in the other with the same hash.
  inline bool sameChunks(const terrain::TerrainVolume& a, const terrain::TerrainVolume& b)
  {
    const terrain::TerrainVolume::ChunkMap left = a.snapshotChunks();
    const terrain::TerrainVolume::ChunkMap right = b.snapshotChunks();

    if (left.size() != right.size())
    {
      return false;
    }

    for (const auto& [coord, chunk] : left)
    {
      const auto match = right.find(coord);

      if (match == right.end() || match->second->hash() != chunk->hash())
      {
        return false;
      }
    }

    return true;
  }

  inline TerrainPointer pressedAt(const bool wasPressed, const bool flying = false)
  {
    return makeTerrainPointer(true, wasPressed, true, false, flying, false);
  }

  struct Frames
  {
    Env env;
    Recorder recorder;
    TerrainBrushSession session;
    int templateCalls = 0;
    bool editable = true;

    TerrainBrushSession::Advance frame(const TerrainPointer& pointer, const std::optional<terrain::RayHit>& hit)
    {
      return session.advance(terrainId, env.volume, editable, pointer, hit,
                             [this] {
                               ++templateCalls;
                               return templateOp(terrain::BrushOp::Kind::add);
                             },
                             recorder.sender());
    }

    size_t brushCount() const
    {
      size_t count = 0;

      for (const auto& payload : recorder.payloads())
      {
        count += payload.kind == terrain::TerrainEditKind::brush ? 1 : 0;
      }

      return count;
    }
  };
}

#endif
