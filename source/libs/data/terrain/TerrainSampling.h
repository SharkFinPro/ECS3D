#ifndef TERRAINSAMPLING_H
#define TERRAINSAMPLING_H

#include "TerrainChunk.h"
#include "TerrainTypes.h"
#include "TerrainVolume.h"
#include <glm/vec3.hpp>
#include <array>
#include <cstddef>
#include <memory>

namespace terrain
{
  inline constexpr float minGradientLength = 0.05f;

  // Caches the chunks around the last query. After the volume is edited a live Sampler may mix old and new chunks
  // within one cell, so build a new Sampler after edits. Not thread safe. A position that is not finite reads as air.
  class Sampler
  {
  public:
    explicit Sampler(const TerrainVolume& volume);

    // The Sampler keeps a reference to the volume, so it must not be built from a temporary.
    explicit Sampler(TerrainVolume&&) = delete;

    // Trilinear distance in voxels at a terrain-local position.
    [[nodiscard]] float distance(glm::vec3 local);

    // Analytic gradient of the trilinear field, in voxels per local unit.
    [[nodiscard]] glm::vec3 gradient(glm::vec3 local);

    // Distance in local units: distance over the gradient length, floored so it never divides by zero and clamped to
    // the band, since clamped corners shrink the gradient and d/|g| would overshoot near the band edge.
    [[nodiscard]] float metricDistance(glm::vec3 local);

  private:
    struct CacheSlot
    {
      ChunkCoord coord{};
      std::shared_ptr<const TerrainChunk> chunk;
      bool valid = false;
    };

    struct Cell
    {
      std::array<float, 8> corners{};
      glm::vec3 fraction{ 0.0f };
    };

    [[nodiscard]] Cell fetchCell(glm::vec3 local);

    [[nodiscard]] static float distanceOf(const Cell& cell);

    [[nodiscard]] glm::vec3 gradientOf(const Cell& cell) const;

    [[nodiscard]] int16_t sampleDistance(glm::ivec3 sample);

    const TerrainVolume& m_volume;
    std::array<CacheSlot, 8> m_cache;
    size_t m_nextSlot = 0;
  };
}

#endif //TERRAINSAMPLING_H
