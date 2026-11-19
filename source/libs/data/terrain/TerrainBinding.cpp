#include "TerrainBinding.h"
#include "TerrainStore.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/collisions/Terrain.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"
#include <cstddef>
#include <memory>

namespace terrain
{
  namespace
  {
    // Most objects are not terrain, so a subtype check on the map entry is far cheaper than the refcounted,
    // dynamic_cast-checked getComponent<Terrain>.
    const std::shared_ptr<Component>* terrainEntryOf(const Object& object)
    {
      const auto& components = object.getComponents();
      const auto collider = components.find(ComponentType::collider);
      if (collider == components.end() || collider->second->getSubType() != ComponentType::SubComponentType_terrain)
      {
        return nullptr;
      }

      return &collider->second;
    }

    void bindOne(const TerrainStore& store, Terrain& component)
    {
      const std::shared_ptr<const TerrainVolume> found = store.find(component.getTerrainUUID());
      if (component.volume() == found && !component.volumeExpired())
      {
        return;
      }

      component.setVolume(found);
      component.invalidateBounds();
    }
  }

  void TerrainBinder::bind(const TerrainStore& store, const SceneManager& scenes)
  {
    if (m_structureRevision != ObjectManager::structureRevision() || scenesChanged(scenes))
    {
      rescan(scenes);
    }

    for (const auto& cached : m_terrains)
    {
      if (const auto component = cached.lock())
      {
        bindOne(store, *component);
      }
    }
  }

  bool TerrainBinder::scenesChanged(const SceneManager& scenes) const
  {
    const auto& current = scenes.getScenes();
    if (current.size() != m_managers.size())
    {
      return true;
    }

    std::size_t index = 0;
    for (const auto& [sceneUUID, scene] : current)
    {
      if (scene->getObjectManager().get() != m_managers[index++])
      {
        return true;
      }
    }

    return false;
  }

  void TerrainBinder::rescan(const SceneManager& scenes)
  {
    m_structureRevision = ObjectManager::structureRevision();
    m_managers.clear();
    m_terrains.clear();

    for (const auto& [sceneUUID, scene] : scenes.getScenes())
    {
      const auto objectManager = scene->getObjectManager();
      m_managers.push_back(objectManager.get());

      for (const auto& object : objectManager->getAllObjects())
      {
        if (const auto entry = terrainEntryOf(*object))
        {
          m_terrains.push_back(std::static_pointer_cast<Terrain>(*entry));
        }
      }
    }
  }

  void bindTerrainVolumes(const TerrainStore& store, const SceneManager& scenes)
  {
    TerrainBinder().bind(store, scenes);
  }

  void invalidateTerrainBounds(const SceneManager& scenes, const uuids::uuid& terrain)
  {
    for (const auto& [sceneUUID, scene] : scenes.getScenes())
    {
      for (const auto& object : scene->getObjectManager()->getAllObjects())
      {
        const auto entry = terrainEntryOf(*object);
        if (const auto component = entry ? static_cast<Terrain*>(entry->get()) : nullptr;
            component && component->getTerrainUUID() == terrain)
        {
          component->invalidateBounds();
        }
      }
    }
  }
}
