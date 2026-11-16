#ifndef TERRAINSTREAMERFIXTURES_H
#define TERRAINSTREAMERFIXTURES_H

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

// Shared by the terrain streamer suites: sample terrains and a Send that records what was sent.
namespace terrainStreamerFixtures
{
  using terrain::ChunkCoord;

  inline const auto terrainA = uuids::uuid::from_string("11111111-1111-1111-1111-111111111111").value();
  inline const auto terrainB = uuids::uuid::from_string("22222222-2222-2222-2222-222222222222").value();

  inline std::shared_ptr<const terrain::TerrainChunk> solid(const uint8_t material = 1)
  {
    return terrain::TerrainChunk::uniform(-terrain::airDistance, material);
  }

  inline std::shared_ptr<const terrain::TerrainChunk> noise(const uint32_t seed)
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
  inline void addTerrain(terrain::TerrainStore& store, const uuids::uuid& uuid, const int count,
                         const bool dense = true)
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

  inline const terrain::TerrainStreamer::FocusFor originFocus =
    [](const uuids::uuid&, const terrain::TerrainDescription&) { return glm::vec3(16.0f); };

  inline size_t chunkTotal(const std::vector<Received>& messages)
  {
    size_t total = 0;
    for (const Received& received : messages)
    {
      total += received.payload.entries.size();
    }

    return total;
  }

  inline void pumpUntilDone(terrain::TerrainStreamer& streamer, terrain::TerrainStore& store, Recorder& recorder,
                     const size_t budget = terrain::defaultStreamBudgetBytes)
  {
    for (int i = 0; i < 10000 && streamer.streaming(1); ++i)
    {
      streamer.pump(store, recorder.sender(), budget);
    }
  }
}

#endif //TERRAINSTREAMERFIXTURES_H
