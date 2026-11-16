#ifndef TERRAINBINDING_H
#define TERRAINBINDING_H

#include <uuid.h>

class SceneManager;

namespace terrain
{
  class TerrainStore;

  // Points every Terrain component in every scene at the volume its uuid names in the store (null when the store
  // lacks it). A component already holding that volume is left alone, so a call per tick costs a lookup per terrain.
  // A component whose volume the store dropped is unbound and its bounds invalidated.
  void bindTerrainVolumes(const TerrainStore& store, const SceneManager& scenes);

  // The volume binding keys on the volume's pointer, so an edit in place leaves a component's cached bounds stale.
  // Invalidates the bounds of every Terrain component in every scene that names the terrain.
  void invalidateTerrainBounds(const SceneManager& scenes, const uuids::uuid& terrain);
}

#endif //TERRAINBINDING_H
