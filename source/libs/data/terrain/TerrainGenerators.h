#ifndef TERRAINGENERATORS_H
#define TERRAINGENERATORS_H

#include "TerrainOps.h"
#include "TerrainVolume.h"
#include <glm/vec2.hpp>
#include <cstdint>
#include <span>

namespace terrain
{
  struct HeightmapView
  {
    std::span<const uint16_t> pixels;  // row-major, width * height entries
    int width = 0;
    int height = 0;
  };

  // Both generators write sample columns x in [0, columns) and z in [0, rows), heights in terrain-local units, from
  // the chunk boundary at or below the lowest surface minus thickness up to the first sample above the band. Every
  // sample in that box is overwritten, so chunks below the surface collapse to uniform solid; samples outside it are
  // kept. Both return empty edits for an invalid argument or a box over maxEditChunks.
  [[nodiscard]] ChunkEdits fillFlat(const TerrainVolume& volume, glm::ivec2 sizeSamples, float groundHeight,
                                    float thickness, uint8_t material);

  // Pixel (u, v) sits on column x = u, z = v and has height pixel / 65535 * heightScale. The distance is the first
  // order estimate (y - h) / sqrt(1 + |grad h|^2).
  [[nodiscard]] ChunkEdits fillFromHeightmap(const TerrainVolume& volume, const HeightmapView& heightmap,
                                             float heightScale, float thickness, uint8_t material);
}

#endif //TERRAINGENERATORS_H
