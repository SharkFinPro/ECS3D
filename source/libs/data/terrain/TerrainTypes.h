#ifndef TERRAINTYPES_H
#define TERRAINTYPES_H

#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace terrain
{
  inline constexpr int chunkSize = 32;
  inline constexpr int samplesPerChunk = chunkSize * chunkSize * chunkSize;
  inline constexpr float bandVoxels = 4.0f;
  inline constexpr int distanceScale = 8191;
  inline constexpr uint8_t reservedMaterial = 255;
  inline constexpr int16_t airDistance = static_cast<int16_t>(bandVoxels * distanceScale);

  [[nodiscard]] inline int16_t encodeDistance(const float voxels)
  {
    if (std::isnan(voxels))
    {
      return airDistance;
    }

    const float clamped = std::clamp(voxels, -bandVoxels, bandVoxels);
    return static_cast<int16_t>(std::lround(clamped * static_cast<float>(distanceScale)));
  }

  [[nodiscard]] inline float decodeDistance(const int16_t distance)
  {
    return static_cast<float>(distance) / static_cast<float>(distanceScale);
  }

  struct ChunkCoord
  {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;

    bool operator==(const ChunkCoord&) const = default;
  };

  [[nodiscard]] inline int32_t floorDiv(const int32_t value, const int32_t divisor)
  {
    int32_t quotient = value / divisor;

    if (value % divisor != 0 && ((value < 0) != (divisor < 0)))
    {
      --quotient;
    }

    return quotient;
  }

  [[nodiscard]] inline ChunkCoord chunkOfSample(const glm::ivec3 sample)
  {
    return { floorDiv(sample.x, chunkSize), floorDiv(sample.y, chunkSize), floorDiv(sample.z, chunkSize) };
  }

  [[nodiscard]] constexpr int sampleIndex(const int x, const int y, const int z)
  {
    return x + chunkSize * (y + chunkSize * z);
  }

  [[nodiscard]] inline int indexInChunk(const glm::ivec3 sample)
  {
    const ChunkCoord chunk = chunkOfSample(sample);
    return sampleIndex(sample.x - chunk.x * chunkSize, sample.y - chunk.y * chunkSize,
                       sample.z - chunk.z * chunkSize);
  }

  [[nodiscard]] inline glm::ivec3 chunkOrigin(const ChunkCoord coord)
  {
    return { coord.x * chunkSize, coord.y * chunkSize, coord.z * chunkSize };
  }

  // max is exclusive.
  struct SampleBox
  {
    glm::ivec3 min{ 0 };
    glm::ivec3 max{ 0 };

    [[nodiscard]] bool empty() const
    {
      return min.x >= max.x || min.y >= max.y || min.z >= max.z;
    }
  };

  [[nodiscard]] inline std::vector<ChunkCoord> chunksTouched(const SampleBox& box)
  {
    std::vector<ChunkCoord> touched;

    if (box.empty())
    {
      return touched;
    }

    const ChunkCoord first = chunkOfSample(box.min);
    const ChunkCoord last = chunkOfSample(box.max - glm::ivec3(1));

    for (int32_t z = first.z; z <= last.z; ++z)
    {
      for (int32_t y = first.y; y <= last.y; ++y)
      {
        for (int32_t x = first.x; x <= last.x; ++x)
        {
          touched.push_back({ x, y, z });
        }
      }
    }

    return touched;
  }
}

template <>
struct std::hash<terrain::ChunkCoord>
{
  size_t operator()(const terrain::ChunkCoord& coord) const noexcept
  {
    uint64_t hash = static_cast<uint32_t>(coord.x) * 0x9E3779B97F4A7C15ull;
    hash ^= hash >> 29;
    hash += static_cast<uint32_t>(coord.y) * 0xC2B2AE3D27D4EB4Full;
    hash ^= hash >> 32;
    hash *= 0xBF58476D1CE4E5B9ull;
    hash += static_cast<uint32_t>(coord.z) * 0x94D049BB133111EBull;
    hash ^= hash >> 31;
    hash *= 0xD6E8FEB86659FD93ull;
    hash ^= hash >> 32;
    return static_cast<size_t>(hash);
  }
};

#endif //TERRAINTYPES_H
