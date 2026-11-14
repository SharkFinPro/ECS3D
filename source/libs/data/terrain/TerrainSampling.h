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

  // Caches the chunks around the last query, so build a new Sampler after the volume is edited.
  class Sampler
  {
  public:
    explicit Sampler(const TerrainVolume& volume);

    // Trilinear distance in voxels at a terrain-local position.
    [[nodiscard]] float distance(glm::vec3 local);

    // Analytic gradient of the trilinear field, in voxels per local unit.
    [[nodiscard]] glm::vec3 gradient(glm::vec3 local);

    // Distance in local units: distance over the gradient length, floored so it never divides by zero.
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

    [[nodiscard]] int16_t sampleDistance(glm::ivec3 sample);

    const TerrainVolume& m_volume;
    std::array<CacheSlot, 8> m_cache;
    size_t m_nextSlot = 0;
  };
}

#endif //TERRAINSAMPLING_H
