#ifndef TERRAINBINDING_H
#define TERRAINBINDING_H

class SceneManager;

namespace terrain
{
  class TerrainStore;

  // Points every Terrain component in every scene at the volume its uuid names in the store (null when the store
  // lacks it). A component already holding that volume is left alone, so a call per tick costs a lookup per terrain.
  void bindTerrainVolumes(const TerrainStore& store, const SceneManager& scenes);
}

#endif //TERRAINBINDING_H
