#include "TerrainSurfaceSampling.h"
#include <glm/common.hpp>
#include <glm/vector_relational.hpp>
#include <algorithm>
#include <limits>

namespace terrain
{
  namespace
  {
    constexpr uint32_t maxFillLod = 8;

    int32_t clampToInt(const int64_t value)
    {
      return static_cast<int32_t>(std::clamp<int64_t>(value, std::numeric_limits<int32_t>::min(),
                                                      std::numeric_limits<int32_t>::max()));
    }

    int64_t floorDiv64(const int64_t value, const int64_t divisor)
    {
      int64_t quotient = value / divisor;

      if (value % divisor != 0 && value < 0)
      {
        --quotient;
      }

      return quotient;
    }

    int64_t roundDown(const int64_t value)
    {
      return floorDiv64(value, surfaceBoundsStep) * surfaceBoundsStep;
    }

    int64_t roundUp(const int64_t value)
    {
      return -roundDown(-value);
    }

    bool chunkLess(const ChunkCoord& a, const ChunkCoord& b)
    {
      if (a.z != b.z)
      {
        return a.z < b.z;
      }

      return a.y != b.y ? a.y < b.y : a.x < b.x;
    }

    uint64_t chunksOnAxis(const int32_t minimum, const int32_t maximum)
    {
      const int64_t samples = static_cast<int64_t>(maximum) - minimum;

      if (samples < 2)
      {
        return 0;
      }

      const uint64_t cells = static_cast<uint64_t>(samples - 1);
      return (cells + surfaceChunkCells - 1) / surfaceChunkCells;
    }

    void fillRow(const TerrainChunk* chunk, const int startIndex, const int count, float* distance, uint8_t* material)
    {
      if (!chunk)
      {
        std::fill_n(distance, count, decodeDistance(airDistance));
        std::fill_n(material, count, uint8_t{ 0 });
        return;
      }

      if (chunk->isUniform())
      {
        std::fill_n(distance, count, decodeDistance(chunk->distanceAtIndex(0)));
        std::fill_n(material, count, chunk->materialAtIndex(0));
        return;
      }

      const auto distances = chunk->distances();
      const auto materials = chunk->materials();

      for (int i = 0; i < count; ++i)
      {
        distance[i] = decodeDistance(distances[static_cast<size_t>(startIndex + i)]);
        material[i] = materials[static_cast<size_t>(startIndex + i)];
      }
    }

    const TerrainChunk* findChunk(const TerrainVolume::ChunkMap& chunks, const ChunkCoord coord)
    {
      const auto found = chunks.find(coord);
      return found == chunks.end() ? nullptr : found->second.get();
    }
  }

  SampleBox surfaceBounds(const SampleBox& occupied)
  {
    if (occupied.empty())
    {
      return {};
    }

    SampleBox bounds;

    for (int axis = 0; axis < 3; ++axis)
    {
      bounds.min[axis] = clampToInt(roundDown(static_cast<int64_t>(occupied.min[axis]) - 1));
      bounds.max[axis] = clampToInt(roundUp(static_cast<int64_t>(occupied.max[axis]) + 1));
    }

    return bounds;
  }

  bool containsBox(const SampleBox& outer, const SampleBox& inner)
  {
    if (inner.empty())
    {
      return true;
    }

    return glm::all(glm::greaterThanEqual(inner.min, outer.min)) && glm::all(glm::lessThanEqual(inner.max, outer.max));
  }

  uint64_t surfaceChunkCount(const SampleBox& bounds)
  {
    const uint64_t x = chunksOnAxis(bounds.min.x, bounds.max.x);
    const uint64_t y = chunksOnAxis(bounds.min.y, bounds.max.y);
    const uint64_t z = chunksOnAxis(bounds.min.z, bounds.max.z);

    if (x == 0 || y == 0 || z == 0)
    {
      return 0;
    }

    // Three axes of 2^27 chunks would wrap, so saturate; any such count is far past the cap.
    constexpr uint64_t limit = std::numeric_limits<uint64_t>::max();

    if (x > limit / y)
    {
      return limit;
    }

    return x * y > limit / z ? limit : x * y * z;
  }

  std::vector<ChunkCoord> changedChunks(const TerrainVolume::ChunkMap& before, const TerrainVolume::ChunkMap& after)
  {
    std::vector<ChunkCoord> changed;

    for (const auto& [coord, chunk] : before)
    {
      const auto found = after.find(coord);

      if (found == after.end() || found->second != chunk)
      {
        changed.push_back(coord);
      }
    }

    for (const auto& entry : after)
    {
      if (!before.contains(entry.first))
      {
        changed.push_back(entry.first);
      }
    }

    std::ranges::sort(changed, chunkLess);
    return changed;
  }

  SampleBox chunkSampleBounds(const std::span<const ChunkCoord> chunks)
  {
    if (chunks.empty())
    {
      return {};
    }

    glm::ivec3 lowest(std::numeric_limits<int32_t>::max());
    glm::ivec3 highest(std::numeric_limits<int32_t>::min());

    for (const ChunkCoord& coord : chunks)
    {
      const glm::ivec3 origin = chunkOrigin(coord);
      lowest = glm::min(lowest, origin);
      highest = glm::max(highest, origin + glm::ivec3(chunkSize));
    }

    return { lowest, highest };
  }

  bool fillSurfaceSamples(const TerrainVolume::ChunkMap& chunks, const SampleBox& region, const uint32_t lod,
                          const std::span<float> distance, const std::span<uint8_t> material)
  {
    if (region.empty() || lod > maxFillLod)
    {
      return false;
    }

    const glm::ivec3 size = region.max - region.min;
    const uint64_t count = static_cast<uint64_t>(size.x) * static_cast<uint64_t>(size.y) *
                           static_cast<uint64_t>(size.z);

    if (distance.size() != count || material.size() != count)
    {
      return false;
    }

    if (lod > 0)
    {
      const int64_t stride = int64_t{ 1 } << lod;
      size_t index = 0;

      for (int z = 0; z < size.z; ++z)
      {
        for (int y = 0; y < size.y; ++y)
        {
          for (int x = 0; x < size.x; ++x, ++index)
          {
            const glm::ivec3 sample(clampToInt((region.min.x + int64_t{ x }) * stride),
                                    clampToInt((region.min.y + int64_t{ y }) * stride),
                                    clampToInt((region.min.z + int64_t{ z }) * stride));
            const TerrainChunk* chunk = findChunk(chunks, chunkOfSample(sample));

            if (chunk)
            {
              const int local = indexInChunk(sample);
              distance[index] = decodeDistance(chunk->distanceAtIndex(local));
              material[index] = chunk->materialAtIndex(local);
            }
            else
            {
              distance[index] = decodeDistance(airDistance);
              material[index] = 0;
            }
          }
        }
      }

      return true;
    }

    const ChunkCoord first = chunkOfSample(region.min);
    const ChunkCoord last = chunkOfSample(region.max - glm::ivec3(1));

    for (int32_t cz = first.z; cz <= last.z; ++cz)
    {
      for (int32_t cy = first.y; cy <= last.y; ++cy)
      {
        for (int32_t cx = first.x; cx <= last.x; ++cx)
        {
          const ChunkCoord coord{ cx, cy, cz };
          const TerrainChunk* chunk = findChunk(chunks, coord);
          const glm::ivec3 origin = chunkOrigin(coord);
          const glm::ivec3 low = glm::max(region.min, origin);
          const glm::ivec3 high = glm::min(region.max, origin + glm::ivec3(chunkSize));
          const int rowLength = high.x - low.x;

          for (int z = low.z; z < high.z; ++z)
          {
            for (int y = low.y; y < high.y; ++y)
            {
              const size_t dst = static_cast<size_t>(low.x - region.min.x) +
                                 static_cast<size_t>(size.x) * (static_cast<size_t>(y - region.min.y) +
                                 static_cast<size_t>(size.y) * static_cast<size_t>(z - region.min.z));
              const int src = sampleIndex(low.x - origin.x, y - origin.y, z - origin.z);
              fillRow(chunk, src, rowLength, distance.data() + dst, material.data() + dst);
            }
          }
        }
      }
    }

    return true;
  }
}
