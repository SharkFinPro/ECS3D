#ifndef DEFAULTTERRAINTILES_H
#define DEFAULTTERRAINTILES_H

#include <glm/vec3.hpp>
#include <terrain/TerrainTypes.h>
#include <array>
#include <cstdint>

namespace terrain
{
  class TerrainStore;
  class TerrainVolume;
}

// The built-in Terrain Basin: one landscape cut into a 3 x 3 grid of terrain tiles, +z north. A flat arena in the
// middle is ringed by a rim that rises into terraces to the north, a steep bank to the east, moguls in the south-west
// and a dry riverbed crossing in from the west. Neighboring tiles overlap by three columns, the shared seam column and
// one apron column on each side, so every tile samples one global height function and meets its neighbor exactly.
// A tile leaves open only the sides that face a neighbor (Terrain::getOpenSides): the outermost column there only
// shapes the normals of the surface inside it, which makes both tiles shade a seam alike. The sides on the world's
// perimeter stay closed, so the basin ends in an ordinary closed edge. Positions are sample columns (voxels) unless a
// name says world.
namespace basin
{
  inline constexpr int tilesPerSide = 3;
  inline constexpr int tileCount = tilesPerSide * tilesPerSide;

  // Samples per tile side (four whole chunks, which an open side needs filled), and the step between one tile's first
  // sample and its neighbor's.
  inline constexpr int tileColumns = 128;
  inline constexpr int tileStride = tileColumns - 3;
  inline constexpr int worldColumns = tilesPerSide * tileStride + 3;

  // An open side needs the data to fill its whole chunk box, and the overlap is the seam plus one apron per side.
  static_assert(tileColumns % terrain::chunkSize == 0);
  static_assert(tileStride == tileColumns - 3);

  // World units per voxel, the arena floor and the heightmap's full scale, in voxels.
  inline constexpr float voxelSize = 1.5f;
  inline constexpr int groundHeight = 24;
  inline constexpr int maxHeight = 96;

  // The scene's knobs: the tower over the arena and the stack over each outer tile are columns x columns bodies
  // wide and layers tall, one body every stackSpacing world units.
  inline constexpr int towerColumns = 5;
  inline constexpr int towerLayers = 12;
  inline constexpr int stackColumns = 3;
  inline constexpr int stackLayers = 5;
  inline constexpr float stackSpacing = 5.0f;

  // The ground height, in voxels, at global sample column (x, z).
  [[nodiscard]] float groundAt(float x, float z);

  // The Terrain object's position for tile (row, col): the basin is centered on the world origin in x and z with the
  // arena floor at world y = 0.
  [[nodiscard]] glm::vec3 tileOrigin(int row, int col);

  [[nodiscard]] float worldGroundY(float worldX, float worldZ);

  // The terrain::TerrainSide bits tile (row, col) leaves open: the sides that face another tile.
  [[nodiscard]] uint8_t tileOpenSides(int row, int col);
}

// Fills the nine tiles, in row-major order (index = row * 3 + column); a null entry is skipped. Deterministic. Each
// volume must already use basin::voxelSize.
void generateBasinTiles(const std::array<terrain::TerrainVolume*, basin::tileCount>& tiles);

// Generates every basin tile the store holds a terrain for and sets each description's chunk count to what it holds.
// A tile missing from the store is skipped and other terrains are left alone.
void installBasinTerrains(terrain::TerrainStore& store);

#endif //DEFAULTTERRAINTILES_H
