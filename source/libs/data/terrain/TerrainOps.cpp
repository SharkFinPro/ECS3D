#include "TerrainOps.h"
#include <glm/common.hpp>
#include <algorithm>
#include <stdexcept>

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

  ChunkEdits editSamples(const TerrainVolume& volume, const SampleBox& box, const SampleEdit& edit)
  {
    if (box.empty())
    {
      return {};
    }

    const ChunkCoord first = chunkOfSample(box.min);
    const ChunkCoord last = chunkOfSample(box.max - glm::ivec3(1));

    if (!isValidChunkCoord(first) || !isValidChunkCoord(last))
    {
      return {};
    }

    const int64_t extentX = static_cast<int64_t>(last.x) - first.x + 1;
    const int64_t extentY = static_cast<int64_t>(last.y) - first.y + 1;
    const int64_t extentZ = static_cast<int64_t>(last.z) - first.z + 1;

    if (extentX > maxEditChunks || extentY > maxEditChunks || extentZ > maxEditChunks ||
        extentX * extentY * extentZ > maxEditChunks)
    {
      return {};
    }

    ChunkEdits edits;

    for (int32_t cz = first.z; cz <= last.z; ++cz)
    {
      for (int32_t cy = first.y; cy <= last.y; ++cy)
      {
        for (int32_t cx = first.x; cx <= last.x; ++cx)
        {
          const ChunkCoord coord{ cx, cy, cz };
          const auto current = volume.chunk(coord);

          std::vector<int16_t> distances(samplesPerChunk, airDistance);
          std::vector<uint8_t> materials(samplesPerChunk, 0);

          if (current)
          {
            for (int i = 0; i < samplesPerChunk; ++i)
            {
              distances[static_cast<size_t>(i)] = current->distanceAtIndex(i);
              materials[static_cast<size_t>(i)] = current->materialAtIndex(i);
            }
          }

          const glm::ivec3 origin = chunkOrigin(coord);
          const glm::ivec3 low = glm::max(box.min, origin);
          const glm::ivec3 high = glm::min(box.max, origin + glm::ivec3(chunkSize));

          for (int z = low.z; z < high.z; ++z)
          {
            for (int y = low.y; y < high.y; ++y)
            {
              for (int x = low.x; x < high.x; ++x)
              {
                const auto index = static_cast<size_t>(sampleIndex(x - origin.x, y - origin.y, z - origin.z));
                const uint8_t inputMaterial = materials[index];
                int16_t distance = distances[index];
                uint8_t material = inputMaterial;

                edit({ x, y, z }, distance, material);

                distances[index] = static_cast<int16_t>(
                  std::clamp(static_cast<int>(distance), -static_cast<int>(airDistance), static_cast<int>(airDistance)));
                materials[index] = material == reservedMaterial ? inputMaterial : material;
              }
            }
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
}
