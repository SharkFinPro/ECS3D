#include "DefaultTerrainPaint.h"
#include <terrain/TerrainChunk.h>
#include <terrain/TerrainGenerators.h>
#include <terrain/TerrainOps.h>
#include <terrain/TerrainTypes.h>
#include <terrain/TerrainVolume.h>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

namespace defaultterrain
{
  namespace
  {
    std::vector<terrain::ChunkCoord> denseChunks(const terrain::TerrainVolume& volume)
    {
      std::vector<terrain::ChunkCoord> dense;
      for (const auto& [coord, chunk] : volume.chunks())
      {
        if (!chunk->isUniform())
        {
          dense.push_back(coord);
        }
      }

      std::ranges::sort(dense, [](const terrain::ChunkCoord& a, const terrain::ChunkCoord& b)
      {
        return std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z);
      });

      return dense;
    }

    // One chunk's distances for the slope reads; a neighbor outside the chunk comes from the volume. Reads are
    // clamped to the ground's columns so the outermost samples do not see the world beyond the box as air.
    class SlopeReader
    {
    public:
      SlopeReader(const terrain::TerrainVolume& volume, const terrain::ChunkCoord coord,
                  const std::vector<int16_t>& distances, const int columns)
        : m_volume(volume), m_origin(terrain::chunkOrigin(coord)), m_distances(distances), m_columns(columns)
      {}

      [[nodiscard]] float normalY(const glm::ivec3 sample) const
      {
        const float gradX = static_cast<float>(distanceAt(sample + glm::ivec3(1, 0, 0)) -
                                               distanceAt(sample - glm::ivec3(1, 0, 0)));
        const float gradY = static_cast<float>(distanceAt(sample + glm::ivec3(0, 1, 0)) -
                                               distanceAt(sample - glm::ivec3(0, 1, 0)));
        const float gradZ = static_cast<float>(distanceAt(sample + glm::ivec3(0, 0, 1)) -
                                               distanceAt(sample - glm::ivec3(0, 0, 1)));
        const float length = std::sqrt(gradX * gradX + gradY * gradY + gradZ * gradZ);

        return length > 0.0f ? gradY / length : 1.0f;
      }

    private:
      [[nodiscard]] int distanceAt(glm::ivec3 sample) const
      {
        sample.x = std::clamp(sample.x, 0, m_columns - 1);
        sample.z = std::clamp(sample.z, 0, m_columns - 1);
        const glm::ivec3 local = sample - m_origin;
        const bool inside = local.x >= 0 && local.y >= 0 && local.z >= 0 && local.x < terrain::chunkSize &&
                            local.y < terrain::chunkSize && local.z < terrain::chunkSize;

        const auto index = static_cast<size_t>(terrain::sampleIndex(local.x, local.y, local.z));

        return static_cast<int>(inside ? m_distances[index] : m_volume.distanceAtSample(sample));
      }

      const terrain::TerrainVolume& m_volume;
      glm::ivec3 m_origin;
      const std::vector<int16_t>& m_distances;
      int m_columns;
    };

    bool isSurfaceSample(const int16_t distance)
    {
      return distance < terrain::airDistance && distance > -terrain::airDistance;
    }

    // The chunk repainted, or null when no material changed.
    std::shared_ptr<const terrain::TerrainChunk> paintChunk(
      const terrain::TerrainVolume& volume, const terrain::ChunkCoord coord, const int columns,
      const std::function<uint8_t(glm::ivec3, float)>& materialFor)
    {
      const auto source = volume.chunk(coord);
      const glm::ivec3 origin = terrain::chunkOrigin(coord);
      const std::vector<int16_t> distances(source->distances().begin(), source->distances().end());
      std::vector<uint8_t> materials(source->materials().begin(), source->materials().end());
      const SlopeReader reader(volume, coord, distances, columns);
      bool changed = false;

      for (int z = 0; z < terrain::chunkSize; ++z)
      {
        for (int y = 0; y < terrain::chunkSize; ++y)
        {
          for (int x = 0; x < terrain::chunkSize; ++x)
          {
            const auto index = static_cast<size_t>(terrain::sampleIndex(x, y, z));
            if (!isSurfaceSample(distances[index]))
            {
              continue;
            }

            const glm::ivec3 sample = origin + glm::ivec3(x, y, z);
            const uint8_t material = materialFor(sample, reader.normalY(sample));
            changed = changed || material != materials[index];
            materials[index] = material;
          }
        }
      }

      return changed ? terrain::TerrainChunk::dense(distances, std::move(materials)) : nullptr;
    }
  }

  void fillHeights(terrain::TerrainVolume& volume, const std::vector<float>& heights, const int columns,
                   const float maxHeight, const float thickness, const uint8_t material)
  {
    std::vector<uint16_t> pixels(heights.size());
    for (size_t index = 0; index < pixels.size(); ++index)
    {
      pixels[index] = static_cast<uint16_t>(std::lround(heights[index] / maxHeight * 65535.0f));
    }

    const terrain::HeightmapView view{ pixels, columns, columns };
    terrain::commit(volume, terrain::fillFromHeightmap(volume, view, maxHeight * volume.voxelSize(),
                                                       thickness * volume.voxelSize(), material));
  }

  void paintSurface(terrain::TerrainVolume& volume, const int columns,
                    const std::function<uint8_t(glm::ivec3 sample, float normalY)>& materialFor)
  {
    // Every chunk reads the unedited volume, so a sample's slope never sees a neighbor already repainted.
    terrain::ChunkEdits edits;

    for (const auto& coord : denseChunks(volume))
    {
      if (auto painted = paintChunk(volume, coord, columns, materialFor))
      {
        edits.emplace_back(coord, std::move(painted));
      }
    }

    terrain::commit(volume, edits);
  }
}
