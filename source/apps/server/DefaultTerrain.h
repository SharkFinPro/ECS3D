#ifndef DEFAULTTERRAIN_H
#define DEFAULTTERRAIN_H

namespace terrain
{
  class TerrainStore;
  class TerrainVolume;
}

// The built-in Terrain Showcase: a rolling landscape with a tunnel through a ridge, a natural arch, an overhanging
// cliff and a floating island, features a heightmap cannot hold. Positions are sample coordinates (voxels), so they
// hold for any voxel size.
namespace showcase
{
  inline constexpr int columns = 320;

  inline constexpr int groundHeight = 40;

  // The tunnel runs along z at x = tunnelX through the ridge; its floor is the ground height.
  inline constexpr int tunnelX = 160;
  inline constexpr int tunnelY = 49;
  inline constexpr int tunnelRadius = 9;
  inline constexpr int tunnelStartZ = 95;
  inline constexpr int tunnelEndZ = 225;

  // A half ring in the x-y plane at z = archZ, standing on the ground; its opening is centered on archX.
  inline constexpr int archX = 70;
  inline constexpr int archZ = 70;
  inline constexpr int archRingRadius = 22;
  inline constexpr int archTubeRadius = 7;

  // Spheres sticking out of the mesa wall, in a grid of x by z.
  inline constexpr int overhangX = 210;
  inline constexpr int overhangZ = 255;
  inline constexpr int overhangY = 76;

  inline constexpr int islandX = 70;
  inline constexpr int islandY = 108;
  inline constexpr int islandZ = 250;
}

// The ground only: the heightmap pass, with no carved or added features and no material bands.
void generateShowcaseGround(terrain::TerrainVolume& volume);

// Deterministic; fills the volume with the whole showcase.
void generateShowcaseTerrain(terrain::TerrainVolume& volume);

// Generates the showcase into the store's volume for the built-in project's terrain record and sets the
// description's chunk count to what it holds. Does nothing when the store holds no such terrain.
void installDefaultTerrains(terrain::TerrainStore& store);

#endif //DEFAULTTERRAIN_H
