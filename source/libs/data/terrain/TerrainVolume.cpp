#include "TerrainVolume.h"
#include <glm/common.hpp>
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
  }

  TerrainVolume::TerrainVolume(const float voxelSize)
    : m_voxelSize(voxelSize)
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
      m_chunks.erase(coord);
      return;
    }

    m_chunks[coord] = std::move(chunk);
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

  size_t TerrainVolume::byteSize() const
  {
    size_t bytes = sizeof(TerrainVolume);

    for (const auto& [coord, chunk] : m_chunks)
    {
      bytes += mapEntryBytes + chunk->byteSize();
    }

    return bytes;
  }
}
