#include "TerrainSampling.h"
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>

namespace terrain
{
  namespace
  {
    // Keeps the float to int conversion defined for absurd positions.
    constexpr float maxSamplePosition = 1.0e9f;
  }

  Sampler::Sampler(const TerrainVolume& volume)
    : m_volume(volume)
  {}

  const TerrainChunk* Sampler::chunkAt(const ChunkCoord coord)
  {
    if (const CacheSlot& last = m_cache[m_lastSlot]; last.valid && last.coord == coord)
    {
      return last.chunk.get();
    }

    for (size_t i = 0; i < m_cache.size(); ++i)
    {
      if (m_cache[i].valid && m_cache[i].coord == coord)
      {
        m_lastSlot = i;
        return m_cache[i].chunk.get();
      }
    }

    m_lastSlot = m_nextSlot;
    m_nextSlot = (m_nextSlot + 1) % m_cache.size();

    CacheSlot& slot = m_cache[m_lastSlot];
    slot.coord = coord;
    slot.chunk = m_volume.chunk(coord);
    slot.valid = true;

    return slot.chunk.get();
  }

  int16_t Sampler::sampleDistance(const glm::ivec3 sample)
  {
    const TerrainChunk* chunk = chunkAt(chunkOfSample(sample));

    return chunk ? chunk->distanceAtIndex(indexInChunk(sample)) : airDistance;
  }

  Sampler::Cell Sampler::cellAt(const glm::vec3 local)
  {
    for (const RecentCell& recent : m_recent)
    {
      if (recent.valid && recent.local == local)
      {
        return recent.cell;
      }
    }

    RecentCell& recent = m_recent[m_nextRecent];
    m_nextRecent = (m_nextRecent + 1) % m_recent.size();
    recent.local = local;
    recent.cell = fetchCell(local);
    recent.valid = true;

    return recent.cell;
  }

  Sampler::Cell Sampler::fetchCell(const glm::vec3 local)
  {
    Cell cell;
    glm::vec3 position = local / m_volume.voxelSize();

    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
    {
      cell.corners.fill(bandVoxels);
      return cell;
    }

    position = glm::clamp(position, glm::vec3(-maxSamplePosition), glm::vec3(maxSamplePosition));
    const glm::vec3 base = glm::floor(position);
    cell.fraction = position - base;
    const glm::ivec3 origin(base);

    // Most cells lie inside one chunk, whose eight corners are then read with one lookup.
    const ChunkCoord coord = chunkOfSample(origin);
    const glm::ivec3 inChunk = origin - chunkOrigin(coord);
    if (inChunk.x < chunkSize - 1 && inChunk.y < chunkSize - 1 && inChunk.z < chunkSize - 1)
    {
      const TerrainChunk* chunk = chunkAt(coord);
      if (!chunk || chunk->isUniform())
      {
        cell.corners.fill(decodeDistance(chunk ? chunk->distances().front() : airDistance));
        return cell;
      }

      const int16_t* distances = chunk->distances().data();
      const int first = sampleIndex(inChunk.x, inChunk.y, inChunk.z);
      for (int corner = 0; corner < 8; ++corner)
      {
        const int index = first + sampleIndex(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
        cell.corners[static_cast<size_t>(corner)] = decodeDistance(distances[index]);
      }

      return cell;
    }

    for (int corner = 0; corner < 8; ++corner)
    {
      const glm::ivec3 offset(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
      cell.corners[static_cast<size_t>(corner)] = decodeDistance(sampleDistance(origin + offset));
    }

    return cell;
  }

  float Sampler::distanceOf(const Cell& cell)
  {
    const glm::vec3 f = cell.fraction;
    const auto& c = cell.corners;

    const float x00 = c[0] + (c[1] - c[0]) * f.x;
    const float x10 = c[2] + (c[3] - c[2]) * f.x;
    const float x01 = c[4] + (c[5] - c[4]) * f.x;
    const float x11 = c[6] + (c[7] - c[6]) * f.x;
    const float y0 = x00 + (x10 - x00) * f.y;
    const float y1 = x01 + (x11 - x01) * f.y;

    return y0 + (y1 - y0) * f.z;
  }

  glm::vec3 Sampler::gradientOf(const Cell& cell) const
  {
    const glm::vec3 f = cell.fraction;
    const auto& c = cell.corners;

    const float dx00 = c[1] - c[0];
    const float dx10 = c[3] - c[2];
    const float dx01 = c[5] - c[4];
    const float dx11 = c[7] - c[6];
    const float dx = (dx00 * (1.0f - f.y) + dx10 * f.y) * (1.0f - f.z) + (dx01 * (1.0f - f.y) + dx11 * f.y) * f.z;

    const float x00 = c[0] + dx00 * f.x;
    const float x10 = c[2] + dx10 * f.x;
    const float x01 = c[4] + dx01 * f.x;
    const float x11 = c[6] + dx11 * f.x;
    const float dy = (x10 - x00) * (1.0f - f.z) + (x11 - x01) * f.z;

    const float y0 = x00 + (x10 - x00) * f.y;
    const float y1 = x01 + (x11 - x01) * f.y;
    const float dz = y1 - y0;

    return glm::vec3(dx, dy, dz) / m_volume.voxelSize();
  }

  float Sampler::distance(const glm::vec3 local)
  {
    return distanceOf(cellAt(local));
  }

  glm::vec3 Sampler::gradient(const glm::vec3 local)
  {
    return gradientOf(cellAt(local));
  }

  float Sampler::metricDistance(const glm::vec3 local)
  {
    const Cell cell = cellAt(local);
    const float voxelSize = m_volume.voxelSize();
    const float length = std::max(glm::length(gradientOf(cell)), minGradientLength / voxelSize);
    const float reach = bandVoxels * voxelSize;
    return std::clamp(distanceOf(cell) / length, -reach, reach);
  }
}
