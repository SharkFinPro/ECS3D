#include "TerrainRaycast.h"
#include "TerrainSampling.h"
#include "TerrainTypes.h"
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace terrain
{
  namespace
  {
    // The step cap keeps a smoothed field, whose distance over gradient length can overestimate, from stepping over
    // a surface thinner than the band.
    constexpr float minStepVoxels = 0.05f;
    constexpr float maxStepVoxels = 1.0f;
    constexpr int bisectionSteps = 24;
    constexpr float bisectionToleranceVoxels = 1.0e-5f;
    constexpr float skipEpsilonVoxels = 1.0e-3f;
    constexpr int maxIterations = 1 << 20;
    constexpr float parallelEpsilon = 1.0e-12f;
    constexpr float degenerateNormalLength = 1.0e-6f;

    [[nodiscard]] bool isFinite(const glm::vec3 value)
    {
      return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    [[nodiscard]] glm::vec3 surfaceNormal(Sampler& sampler, const glm::vec3 point, const glm::vec3 direction)
    {
      const glm::vec3 gradient = sampler.gradient(point);
      const float length = glm::length(gradient);

      return length > degenerateNormalLength ? gradient / length : -direction;
    }

    // Slab test against a box in local units. Returns false when the ray misses it entirely.
    [[nodiscard]] bool clipToBox(const glm::vec3 origin, const glm::vec3 direction, const glm::vec3 low,
                                 const glm::vec3 high, float& tEnter, float& tExit)
    {
      for (int axis = 0; axis < 3; ++axis)
      {
        if (std::abs(direction[axis]) < parallelEpsilon)
        {
          if (origin[axis] < low[axis] || origin[axis] > high[axis])
          {
            return false;
          }

          continue;
        }

        const float first = (low[axis] - origin[axis]) / direction[axis];
        const float second = (high[axis] - origin[axis]) / direction[axis];
        tEnter = std::max(tEnter, std::min(first, second));
        tExit = std::min(tExit, std::max(first, second));
      }

      return tEnter <= tExit;
    }

    // The cells whose base sample is in this box have all eight corners in the chunk, so an absent chunk makes them
    // pure air. Cells on the last sample layer reach into the neighbor and are not part of it.
    [[nodiscard]] float airBoxExit(const glm::vec3 position, const glm::vec3 direction, const ChunkCoord coord,
                                   const float voxelSize)
    {
      const glm::vec3 low = voxelSize * glm::vec3(chunkOrigin(coord));
      const glm::vec3 high = low + voxelSize * static_cast<float>(chunkSize - 1);
      float exit = std::numeric_limits<float>::infinity();

      for (int axis = 0; axis < 3; ++axis)
      {
        if (direction[axis] > 0.0f)
        {
          exit = std::min(exit, (high[axis] - position[axis]) / direction[axis]);
        }
        else if (direction[axis] < 0.0f)
        {
          exit = std::min(exit, (low[axis] - position[axis]) / direction[axis]);
        }
      }

      return std::max(exit, 0.0f);
    }
  }

  std::optional<RayHit> raycast(const TerrainVolume& volume, const glm::vec3 origin, const glm::vec3 direction,
                                const float maxDistance)
  {
    if (!isFinite(origin) || !isFinite(direction) || !std::isfinite(maxDistance) || maxDistance < 0.0f)
    {
      return std::nullopt;
    }

    const float directionLength = glm::length(direction);

    if (!std::isfinite(directionLength) || directionLength < parallelEpsilon)
    {
      return std::nullopt;
    }

    const glm::vec3 dir = direction / directionLength;
    const float voxelSize = volume.voxelSize();
    Sampler sampler(volume);

    if (sampler.distance(origin) <= 0.0f)
    {
      return RayHit{ origin, surfaceNormal(sampler, origin, dir), 0.0f };
    }

    const SampleBox bounds = volume.occupiedBounds();

    if (bounds.empty())
    {
      return std::nullopt;
    }

    // Clipping first keeps a huge maxDistance through empty space cheap. The extra sample covers the cells whose
    // base sample is just outside the occupied chunks.
    const glm::vec3 low = voxelSize * glm::vec3(bounds.min - glm::ivec3(1));
    const glm::vec3 high = voxelSize * glm::vec3(bounds.max + glm::ivec3(1));
    float tEnd = maxDistance;
    float t = 0.0f;

    if (!clipToBox(origin, dir, low, high, t, tEnd))
    {
      return std::nullopt;
    }

    const float minStep = minStepVoxels * voxelSize;
    const float maxStep = maxStepVoxels * voxelSize;
    const float skipEpsilon = skipEpsilonVoxels * voxelSize;
    float previousT = t;

    for (int iteration = 0; iteration < maxIterations; ++iteration)
    {
      const glm::vec3 point = origin + t * dir;
      const glm::ivec3 baseSample(glm::floor(point / voxelSize));
      const ChunkCoord baseChunk = chunkOfSample(baseSample);

      if (!volume.chunk(baseChunk))
      {
        const glm::ivec3 inChunk = baseSample - chunkOrigin(baseChunk);

        if (inChunk.x < chunkSize - 1 && inChunk.y < chunkSize - 1 && inChunk.z < chunkSize - 1)
        {
          const float exitT = t + airBoxExit(point, dir, baseChunk, voxelSize);

          if (exitT >= tEnd)
          {
            return std::nullopt;
          }

          previousT = exitT;
          t = std::min(exitT + skipEpsilon, tEnd);
          continue;
        }
      }

      if (sampler.distance(point) <= 0.0f)
      {
        float inside = t;
        float outside = previousT;

        for (int step = 0; step < bisectionSteps && inside - outside > bisectionToleranceVoxels * voxelSize; ++step)
        {
          const float middle = 0.5f * (inside + outside);

          if (sampler.distance(origin + middle * dir) <= 0.0f)
          {
            inside = middle;
          }
          else
          {
            outside = middle;
          }
        }

        const glm::vec3 hitPoint = origin + inside * dir;
        return RayHit{ hitPoint, surfaceNormal(sampler, hitPoint, dir), inside };
      }

      if (t >= tEnd)
      {
        return std::nullopt;
      }

      previousT = t;
      t = std::min(t + std::clamp(sampler.metricDistance(point), minStep, maxStep), tEnd);
    }

    return std::nullopt;
  }
}
