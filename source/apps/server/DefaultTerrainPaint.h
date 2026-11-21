#ifndef DEFAULTTERRAINPAINT_H
#define DEFAULTTERRAINPAINT_H

#include <glm/vec3.hpp>
#include <cstdint>
#include <functional>
#include <vector>

namespace terrain
{
  class TerrainVolume;
}

// The heightmap fill and slope painting the built-in terrains share.
namespace defaultterrain
{
  // The material ids of the built-in palette.
  inline constexpr uint8_t materialGray = 0;
  inline constexpr uint8_t materialGrass = 1;
  inline constexpr uint8_t materialBrown = 2;
  inline constexpr uint8_t materialSand = 4;

  // The solid band under the ground surface, in voxels, and what fills it.
  inline constexpr float groundThickness = 8.0f;
  inline constexpr uint8_t interiorMaterial = materialBrown;

  // Fills the volume from a square grid of ground heights in voxels, row-major as z * columns + x. maxHeight (voxels)
  // is the heightmap's full scale, so heights are quantized to maxHeight / 65535.
  void fillHeights(terrain::TerrainVolume& volume, const std::vector<float>& heights, int columns, float maxHeight,
                   float thickness, uint8_t material);

  // Repaints the surface samples of every dense chunk with the material the callback picks for a sample coordinate
  // and the surface's upward normal there. The slope reads stay inside the first `columns` columns in x and z.
  void paintSurface(terrain::TerrainVolume& volume, int columns,
                    const std::function<uint8_t(glm::ivec3 sample, float normalY)>& materialFor);
}

#endif //DEFAULTTERRAINPAINT_H
