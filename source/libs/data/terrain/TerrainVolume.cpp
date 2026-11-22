#include "TerrainVolume.h"
#include <glm/common.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace terrain
{
  namespace
  {
    // Rough per-entry cost of a hash map node: key, pointer, bucket link and allocator overhead.
    constexpr size_t mapEntryBytes = sizeof(ChunkCoord) + sizeof(std::shared_ptr<const TerrainChunk>) + 32;

    std::atomic<uint64_t> lastRevision{ 0 };

    uint64_t nextRevision()
    {
      return lastRevision.fetch_add(1, std::memory_order_relaxed) + 1;
    }
  }

  TerrainVolume::TerrainVolume(const float voxelSize)
    : m_voxelSize(voxelSize),
      m_revision(nextRevision())
  {
    if (!std::isfinite(voxelSize) || voxelSize <= 0.0f)
    {
      throw std::invalid_argument("TerrainVolume voxel size must be finite and positive");
    }
  }

  float TerrainVolume::voxelSize() const
  {
    return m_voxelSize;
  }

  std::shared_ptr<const TerrainChunk> TerrainVolume::chunk(const ChunkCoord coord) const
  {
    const auto found = m_chunks.find(coord);
    return found == m_chunks.end() ? nullptr : found->second;
  }

  void TerrainVolume::setChunk(const ChunkCoord coord, std::shared_ptr<const TerrainChunk> chunk)
  {
    if (!isValidChunkCoord(coord))
    {
      throw std::out_of_range("terrain chunk coordinate out of range");
    }

    if (!chunk || chunk->isAir())
    {
      if (m_chunks.erase(coord) > 0)
      {
        m_revision = nextRevision();
      }

      return;
    }

    auto& slot = m_chunks[coord];

    if (slot != chunk)
    {
      slot = std::move(chunk);
      m_revision = nextRevision();
    }
  }

  const TerrainVolume::ChunkMap& TerrainVolume::chunks() const
  {
    return m_chunks;
  }

  TerrainVolume::ChunkMap TerrainVolume::snapshotChunks() const
  {
    return m_chunks;
  }

  void TerrainVolume::restoreChunks(ChunkMap chunks)
  {
    for (const auto& entry : chunks)
    {
      if (!isValidChunkCoord(entry.first))
      {
        throw std::out_of_range("terrain chunk coordinate out of range");
      }
    }

    std::erase_if(chunks, [](const auto& entry)
    {
      return !entry.second || entry.second->isAir();
    });

    m_chunks = std::move(chunks);
    m_revision = nextRevision();
  }

  int16_t TerrainVolume::distanceAtSample(const glm::ivec3 sample) const
  {
    const auto found = m_chunks.find(chunkOfSample(sample));
    return found == m_chunks.end() ? airDistance : found->second->distanceAtIndex(indexInChunk(sample));
  }

  uint8_t TerrainVolume::materialAtSample(const glm::ivec3 sample) const
  {
    const auto found = m_chunks.find(chunkOfSample(sample));
    return found == m_chunks.end() ? uint8_t{ 0 } : found->second->materialAtIndex(indexInChunk(sample));
  }

  SampleBox TerrainVolume::occupiedBounds() const
  {
    if (m_chunks.empty())
    {
      return {};
    }

    glm::ivec3 lowest(INT32_MAX);
    glm::ivec3 highest(INT32_MIN);

    for (const auto& [coord, chunk] : m_chunks)
    {
      const glm::ivec3 origin = chunkOrigin(coord);
      lowest = glm::min(lowest, origin);
      highest = glm::max(highest, origin + glm::ivec3(chunkSize));
    }

    return { lowest, highest };
  }

  bool TerrainVolume::isAirWithin(const SampleBox& box) const
  {
    if (box.empty())
    {
      return true;
    }

    const ChunkCoord first = chunkOfSample(box.min);
    const ChunkCoord last = chunkOfSample(box.max - glm::ivec3(1));

    const auto airIn = [&box](const ChunkCoord coord, const TerrainChunk& chunk)
    {
      const glm::ivec3 origin = chunkOrigin(coord);
      return chunk.isAirWithin({ box.min - origin, box.max - origin });
    };

    const int64_t inBox = static_cast<int64_t>(last.x - first.x + 1) * static_cast<int64_t>(last.y - first.y + 1) *
      static_cast<int64_t>(last.z - first.z + 1);

    // Walks the held chunks instead when there are fewer, so a huge box costs no more than the volume holds.
    if (inBox > static_cast<int64_t>(m_chunks.size()))
    {
      return std::ranges::all_of(m_chunks, [&first, &last, &airIn](const auto& entry)
      {
        const ChunkCoord& coord = entry.first;
        const bool inside = coord.x >= first.x && coord.x <= last.x && coord.y >= first.y && coord.y <= last.y &&
          coord.z >= first.z && coord.z <= last.z;

        return !inside || airIn(coord, *entry.second);
      });
    }

    for (int32_t z = first.z; z <= last.z; ++z)
    {
      for (int32_t y = first.y; y <= last.y; ++y)
      {
        for (int32_t x = first.x; x <= last.x; ++x)
        {
          const auto found = m_chunks.find({ x, y, z });
          if (found != m_chunks.end() && !airIn(found->first, *found->second))
          {
            return false;
          }
        }
      }
    }

    return true;
  }

  size_t TerrainVolume::byteSize() const
  {
    size_t bytes = sizeof(TerrainVolume);

    for (const auto& [coord, chunk] : m_chunks)
    {
      bytes += mapEntryBytes + chunk->byteSize();
    }

    return bytes;
  }

  uint64_t TerrainVolume::revision() const
  {
    return m_revision;
  }
}
