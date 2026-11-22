#ifndef DEFAULTTERRAINCAVES_H
#define DEFAULTTERRAINCAVES_H

#include <array>
#include <cstddef>
#include <functional>

namespace terrain
{
  class TerrainVolume;
}

// The Terrain Basin's volumetric features, which a heightmap cannot hold: a tunnel and chamber bored into the terraced
// north rim, a rock arch standing on the arena floor with the floor running through it, and a cavern in the east bank
// that opens to the sky through a shaft and to the arena through a tunnel. Each lies inside one tile's interior, clear
// of the seam columns, so neighboring tiles still agree exactly. Positions are global sample columns (voxels) unless a
// name says world.
namespace basin::caves
{
  inline constexpr int featureCount = 3;

  // The signed distance, in voxels, of the plain ground at a global sample; negative inside the rock.
  using GroundDistance = std::function<float(int x, int y, int z)>;

  // Rewrites the features that lie in tile (row, col) of the volume, whose first sample is global column
  // (offsetX, offsetZ). Each feature's rock is re-evaluated from solid ground, since the heightmap fill leaves only a
  // thin shell.
  void carveTile(terrain::TerrainVolume& volume, int row, int col, int offsetX, int offsetZ,
                 const GroundDistance& ground);

  // Whether a surface sample at global (x, y, z) with the given upward normal is the arch's rock rather than the arena
  // floor it stands on: its walls, its underside and its top.
  [[nodiscard]] bool isArchRock(int x, int y, int z, float normalY);

  // Whether the global column lies over one of the features.
  [[nodiscard]] bool isFeatureColumn(int x, int z);

  // A stack of bodies the scene spawns over a feature: its center and the height of the surface it stands on, in world
  // units, and how many bodies it holds. enclosed says the stack stands inside the terrain, under rock.
  struct Stack
  {
    const char* name;
    float worldX;
    float worldZ;
    float surfaceY;
    bool enclosed;
    int columns;
    int layers;
  };

  inline constexpr size_t stackCount = 4;

  // How far above its surface a stack's lowest layer starts: a small drop, since a chamber is low.
  inline constexpr float stackLift = 4.0f;

  [[nodiscard]] const std::array<Stack, stackCount>& stacks();

  // How far, in world units, a stack's bodies may reach from its center in x and z.
  [[nodiscard]] float stackHalfSpan(const Stack& stack);
}

#endif //DEFAULTTERRAINCAVES_H
