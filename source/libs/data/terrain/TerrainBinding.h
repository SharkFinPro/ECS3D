#ifndef TERRAINBINDING_H
#define TERRAINBINDING_H

#include <uuid.h>
#include <cstdint>
#include <memory>
#include <vector>

class ObjectManager;
class SceneManager;
class Terrain;

namespace terrain
{
  class TerrainStore;

  // Points every Terrain component in every scene at the volume its uuid names in the store (null when the store
  // lacks it). A component already holding that volume is left alone, and a component whose volume the store
  // dropped is unbound and its bounds invalidated. The components a scan found are kept, and the scenes are scanned
  // again only when the scene set or ObjectManager::structureRevision moves, so a call per tick costs a lookup per
  // terrain rather than one per object.
  class TerrainBinder
  {
  public:
    void bind(const TerrainStore& store, const SceneManager& scenes);

  private:
    [[nodiscard]] bool scenesChanged(const SceneManager& scenes) const;
    void rescan(const SceneManager& scenes);

    uint64_t m_structureRevision = 0;
    std::vector<const ObjectManager*> m_managers;
    std::vector<std::weak_ptr<Terrain>> m_terrains;
  };

  // A one-off bind with a full scan, for a caller that keeps no TerrainBinder.
  void bindTerrainVolumes(const TerrainStore& store, const SceneManager& scenes);

  // The volume binding keys on the volume's pointer, so an edit in place leaves a component's cached bounds stale.
  // Invalidates the bounds of every Terrain component in every scene that names the terrain.
  void invalidateTerrainBounds(const SceneManager& scenes, const uuids::uuid& terrain);
}

#endif //TERRAINBINDING_H
