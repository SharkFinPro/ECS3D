#include "TerrainOps.h"
#include <glm/common.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace terrain
{
  namespace
  {
    bool sameContent(const std::shared_ptr<const TerrainChunk>& before, const std::shared_ptr<const TerrainChunk>& after)
    {
      const bool beforeAir = !before || before->isAir();
      const bool afterAir = !after || after->isAir();

      if (beforeAir || afterAir)
      {
        return beforeAir == afterAir;
      }

      return before->isUniform() == after->isUniform() && std::ranges::equal(before->distances(), after->distances()) &&
             std::ranges::equal(before->materials(), after->materials());
    }

    bool withinEditLimits(const SampleBox& box)
    {
      const ChunkCoord first = chunkOfSample(box.min);
      const ChunkCoord last = chunkOfSample(box.max - glm::ivec3(1));

      if (!isValidChunkCoord(first) || !isValidChunkCoord(last))
      {
        return false;
      }

      const int64_t extentX = static_cast<int64_t>(last.x) - first.x + 1;
      const int64_t extentY = static_cast<int64_t>(last.y) - first.y + 1;
      const int64_t extentZ = static_cast<int64_t>(last.z) - first.z + 1;

      return extentX <= maxEditChunks && extentY <= maxEditChunks && extentZ <= maxEditChunks &&
             extentX * extentY * extentZ <= maxEditChunks;
    }

    // Godunov upwind solution of |grad u| = 1 with unit spacing from the three axis-neighbor minima.
    float solveEikonal(float a, float b, float c)
    {
      if (a > b)
      {
        std::swap(a, b);
      }

      if (b > c)
      {
        std::swap(b, c);
      }

      if (a > b)
      {
        std::swap(a, b);
      }

      float u = a + 1.0f;

      if (u > b)
      {
        u = 0.5f * (a + b + std::sqrt(std::max(0.0f, 2.0f - (a - b) * (a - b))));

        if (u > c)
        {
          const float sum = a + b + c;
          const float squares = a * a + b * b + c * c;
          u = (sum + std::sqrt(std::max(0.0f, sum * sum - 3.0f * (squares - 1.0f)))) / 3.0f;
        }
      }

      return u;
    }
  }

  void commit(TerrainVolume& volume, const ChunkEdits& edits)
  {
    for (const auto& edit : edits)
    {
      if (!isValidChunkCoord(edit.first))
      {
        throw std::out_of_range("terrain chunk coordinate out of range");
      }
    }

    for (const auto& edit : edits)
    {
      volume.setChunk(edit.first, edit.second);
    }
  }

  ChunkEdits editSamples(const TerrainVolume& volume, const SampleBox& box, const SampleEdit& edit,
                         const UniformChunkHint& uniformHint, const ChunkSkip& skipChunk)
  {
    if (box.empty())
    {
      return {};
    }

    if (!withinEditLimits(box))
    {
      return {};
    }

    const ChunkCoord first = chunkOfSample(box.min);
    const ChunkCoord last = chunkOfSample(box.max - glm::ivec3(1));

    ChunkEdits edits;

    for (int32_t cz = first.z; cz <= last.z; ++cz)
    {
      for (int32_t cy = first.y; cy <= last.y; ++cy)
      {
        for (int32_t cx = first.x; cx <= last.x; ++cx)
        {
          const ChunkCoord coord{ cx, cy, cz };
          const auto current = volume.chunk(coord);
          const glm::ivec3 origin = chunkOrigin(coord);
          const glm::ivec3 low = glm::max(box.min, origin);
          const glm::ivec3 high = glm::min(box.max, origin + glm::ivec3(chunkSize));

          if (skipChunk && skipChunk({ low, high }))
          {
            continue;
          }

          if (uniformHint && low == origin && high == origin + glm::ivec3(chunkSize))
          {
            int16_t hintDistance = 0;
            uint8_t hintMaterial = 0;

            if (uniformHint(coord, hintDistance, hintMaterial) && hintMaterial != reservedMaterial)
            {
              const auto filled = TerrainChunk::uniform(
                static_cast<int16_t>(std::clamp(static_cast<int>(hintDistance), -static_cast<int>(airDistance),
                                                static_cast<int>(airDistance))),
                hintMaterial);

              if (filled)
              {
                if (!sameContent(current, filled))
                {
                  edits.emplace_back(coord, filled->isAir() ? nullptr : filled);
                }

                continue;
              }
            }
          }

          std::vector<int16_t> distances;
          std::vector<uint8_t> materials;

          if (!current)
          {
            distances.assign(samplesPerChunk, airDistance);
            materials.assign(samplesPerChunk, 0);
          }
          else if (current->isUniform())
          {
            distances.assign(samplesPerChunk, current->distances().front());
            materials.assign(samplesPerChunk, current->materials().front());
          }
          else
          {
            distances.assign(current->distances().begin(), current->distances().end());
            materials.assign(current->materials().begin(), current->materials().end());
          }

          bool changed = false;

          for (int z = low.z; z < high.z; ++z)
          {
            for (int y = low.y; y < high.y; ++y)
            {
              for (int x = low.x; x < high.x; ++x)
              {
                const auto index = static_cast<size_t>(sampleIndex(x - origin.x, y - origin.y, z - origin.z));
                const uint8_t inputMaterial = materials[index];
                const int16_t inputDistance = distances[index];
                int16_t distance = inputDistance;
                uint8_t material = inputMaterial;

                edit({ x, y, z }, distance, material);

                const auto outputDistance = static_cast<int16_t>(
                  std::clamp(static_cast<int>(distance), -static_cast<int>(airDistance), static_cast<int>(airDistance)));
                const uint8_t outputMaterial = material == reservedMaterial ? inputMaterial : material;

                changed = changed || outputDistance != inputDistance || outputMaterial != inputMaterial;
                distances[index] = outputDistance;
                materials[index] = outputMaterial;
              }
            }
          }

          if (!changed)
          {
            continue;
          }

          auto rebuilt = TerrainChunk::dense(std::move(distances), std::move(materials));

          if (sameContent(current, rebuilt))
          {
            continue;
          }

          edits.emplace_back(coord, rebuilt->isAir() ? nullptr : std::move(rebuilt));
        }
      }
    }

    return edits;
  }

  ChunkEdits redistance(const TerrainVolume& volume, const SampleBox& box)
  {
    if (box.empty() || !withinEditLimits(box))
    {
      return {};
    }

    const glm::ivec3 gridMin = box.min - glm::ivec3(1);
    const glm::ivec3 size = box.max - box.min + glm::ivec3(2);
    const int64_t total = static_cast<int64_t>(size.x) * size.y * size.z;

    if (total > maxRedistanceSamples)
    {
      return {};
    }

    const auto at = [&](const int x, const int y, const int z)
    {
      return static_cast<size_t>(x + size.x * (y + size.y * z));
    };

    std::vector<int16_t> encoded(static_cast<size_t>(total));
    std::vector<float> field(static_cast<size_t>(total));

    volume.readDistances({ gridMin, gridMin + size }, encoded);

    for (size_t index = 0; index < encoded.size(); ++index)
    {
      field[index] = decodeDistance(encoded[index]);
    }

    std::vector<uint8_t> fixed(static_cast<size_t>(total), 0);

    for (int z = 0; z < size.z; ++z)
    {
      for (int y = 0; y < size.y; ++y)
      {
        for (int x = 0; x < size.x; ++x)
        {
          const size_t index = at(x, y, z);
          const bool inside = encoded[index] < 0;
          const bool interior = x > 0 && y > 0 && z > 0 && x < size.x - 1 && y < size.y - 1 && z < size.z - 1;
          bool freeze = !interior || encoded[index] == 0;

          for (int dz = -1; dz <= 1 && !freeze; ++dz)
          {
            for (int dy = -1; dy <= 1 && !freeze; ++dy)
            {
              for (int dx = -1; dx <= 1 && !freeze; ++dx)
              {
                freeze = (encoded[at(x + dx, y + dy, z + dz)] < 0) != inside;
              }
            }
          }

          fixed[index] = freeze ? 1 : 0;

          if (!freeze)
          {
            field[index] = inside ? -bandVoxels : bandVoxels;
          }
        }
      }
    }

    for (int repeat = 0; repeat < 2; ++repeat)
    {
      for (int order = 0; order < 8; ++order)
      {
        const bool reverseX = (order & 1) != 0;
        const bool reverseY = (order & 2) != 0;
        const bool reverseZ = (order & 4) != 0;

        for (int k = 1; k < size.z - 1; ++k)
        {
          const int z = reverseZ ? size.z - 1 - k : k;

          for (int j = 1; j < size.y - 1; ++j)
          {
            const int y = reverseY ? size.y - 1 - j : j;

            for (int i = 1; i < size.x - 1; ++i)
            {
              const int x = reverseX ? size.x - 1 - i : i;
              const size_t index = at(x, y, z);

              if (fixed[index] != 0)
              {
                continue;
              }

              const float a = std::min(std::abs(field[at(x - 1, y, z)]), std::abs(field[at(x + 1, y, z)]));
              const float b = std::min(std::abs(field[at(x, y - 1, z)]), std::abs(field[at(x, y + 1, z)]));
              const float c = std::min(std::abs(field[at(x, y, z - 1)]), std::abs(field[at(x, y, z + 1)]));

              // The upwind solution is at least the smallest neighbor plus 1/sqrt(3), so a sample already within
              // that of its smallest neighbor cannot improve.
              if (std::abs(field[index]) <= std::min({ a, b, c }) + 0.5f)
              {
                continue;
              }

              const float magnitude = std::min(std::abs(field[index]), solveEikonal(a, b, c));

              field[index] = std::signbit(field[index]) ? -magnitude : magnitude;
            }
          }
        }
      }
    }

    return editSamples(volume, box, [&](const glm::ivec3 sample, int16_t& distance, uint8_t&)
    {
      const glm::ivec3 local = sample - gridMin;
      const size_t index = at(local.x, local.y, local.z);

      if (fixed[index] == 0 && field[index] != decodeDistance(distance))
      {
        distance = encodeDistance(field[index]);
      }
    });
  }
}
