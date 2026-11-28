#include "TerrainGenerators.h"
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace terrain
{
  namespace
  {
    // Keeps the vertical extent, in voxels, well inside int32_t.
    constexpr double maxExtentVoxels = 1.0e9;

    struct Surface
    {
      float height = 0.0f;
      float inverseNorm = 1.0f;
    };

    template <typename SurfaceAt>
    ChunkEdits generate(const TerrainVolume& volume, const int columns, const int rows, const double minHeight,
                        const double maxHeight, const double maxNorm, const float thickness,
                        const uint8_t material, const SurfaceAt& surfaceAt)
    {
      const float voxelSize = volume.voxelSize();
      const double lowVoxels = std::floor((minHeight - thickness) / voxelSize);
      const double highVoxels = std::ceil(maxHeight / voxelSize + bandVoxels * maxNorm) + 1.0;

      if (!(std::abs(lowVoxels) < maxExtentVoxels) || !(std::abs(highVoxels) < maxExtentVoxels))
      {
        return {};
      }

      const int bottom = floorDiv(static_cast<int32_t>(lowVoxels), chunkSize) * chunkSize;
      const SampleBox box{ { 0, bottom, 0 }, { columns, static_cast<int32_t>(highVoxels), rows } };

      const auto distanceAt = [voxelSize](const int y, const Surface& surface)
      {
        const float above = static_cast<float>(y) * voxelSize - surface.height;
        return encodeDistance(above * surface.inverseNorm / voxelSize);
      };

      // Distance only grows with height, so a chunk is all air when the lowest sample of every column is air, and all
      // solid when the highest is. Those chunks never need their samples visited.
      const auto uniformHint = [&](const ChunkCoord coord, int16_t& distance, uint8_t& hintMaterial)
      {
        const glm::ivec3 origin = chunkOrigin(coord);
        bool allAir = true;
        bool allSolid = true;

        for (int v = origin.z; v < origin.z + chunkSize && (allAir || allSolid); ++v)
        {
          for (int u = origin.x; u < origin.x + chunkSize && (allAir || allSolid); ++u)
          {
            const Surface surface = surfaceAt(u, v);
            allAir = allAir && distanceAt(origin.y, surface) == airDistance;
            allSolid = allSolid && distanceAt(origin.y + chunkSize - 1, surface) == -airDistance;
          }
        }

        if (!allAir && !allSolid)
        {
          return false;
        }

        distance = allAir ? airDistance : static_cast<int16_t>(-airDistance);
        hintMaterial = material;

        return true;
      };

      return editSamples(volume, box, [&](const glm::ivec3 sample, int16_t& distance, uint8_t& sampleMaterial)
      {
        distance = distanceAt(sample.y, surfaceAt(sample.x, sample.z));
        sampleMaterial = material;
      }, uniformHint, {}, true);
    }

    bool validCommon(const float thickness, const uint8_t material)
    {
      return std::isfinite(thickness) && thickness > 0.0f && material != reservedMaterial;
    }
  }

  ChunkEdits fillFlat(const TerrainVolume& volume, const glm::ivec2 sizeSamples, const float groundHeight,
                      const float thickness, const uint8_t material)
  {
    if (sizeSamples.x < 1 || sizeSamples.y < 1 || !std::isfinite(groundHeight) || !validCommon(thickness, material))
    {
      return {};
    }

    return generate(volume, sizeSamples.x, sizeSamples.y, groundHeight, groundHeight, 1.0, thickness, material,
                    [groundHeight](int, int)
    {
      return Surface{ groundHeight, 1.0f };
    });
  }

  ChunkEdits fillFromHeightmap(const TerrainVolume& volume, const HeightmapView& heightmap, const float heightScale,
                               const float thickness, const uint8_t material)
  {
    if (heightmap.width < 1 || heightmap.height < 1 || !std::isfinite(heightScale) || heightScale < 0.0f ||
        !validCommon(thickness, material) ||
        heightmap.pixels.size() != static_cast<size_t>(heightmap.width) * static_cast<size_t>(heightmap.height))
    {
      return {};
    }

    const auto [lowest, highest] = std::ranges::minmax_element(heightmap.pixels);
    const auto toHeight = [heightScale](const uint16_t pixel)
    {
      return static_cast<float>(pixel) / 65535.0f * heightScale;
    };

    const int width = heightmap.width;
    const int rows = heightmap.height;
    const float voxelSize = volume.voxelSize();

    const auto heightAt = [&](const int u, const int v)
    {
      return toHeight(heightmap.pixels[static_cast<size_t>(v) * static_cast<size_t>(width) + static_cast<size_t>(u)]);
    };

    // Central difference, one-sided at the image edge, in terrain-local units.
    const auto slope = [voxelSize](const auto& read, const int index, const int count)
    {
      const int before = std::max(index - 1, 0);
      const int after = std::min(index + 1, count - 1);
      return after == before ? 0.0f : (read(after) - read(before)) / (static_cast<float>(after - before) * voxelSize);
    };

    const auto surfaceAt = [&](const int u, const int v)
    {
      const float gradX = slope([&](const int i) { return heightAt(i, v); }, u, width);
      const float gradZ = slope([&](const int i) { return heightAt(u, i); }, v, rows);
      return Surface{ heightAt(u, v), 1.0f / std::sqrt(1.0f + gradX * gradX + gradZ * gradZ) };
    };

    // Each column's surface is read once per sample of its column, so it is computed once here.
    std::vector<Surface> surfaces(heightmap.pixels.size());
    float smallestInverseNorm = 1.0f;

    for (int v = 0; v < rows; ++v)
    {
      for (int u = 0; u < width; ++u)
      {
        const Surface surface = surfaceAt(u, v);
        surfaces[static_cast<size_t>(v) * static_cast<size_t>(width) + static_cast<size_t>(u)] = surface;
        smallestInverseNorm = std::min(smallestInverseNorm, surface.inverseNorm);
      }
    }

    return generate(volume, width, rows, toHeight(*lowest), toHeight(*highest), 1.0 / smallestInverseNorm, thickness,
                    material, [&surfaces, width](const int u, const int v)
    {
      return surfaces[static_cast<size_t>(v) * static_cast<size_t>(width) + static_cast<size_t>(u)];
    });
  }
}
