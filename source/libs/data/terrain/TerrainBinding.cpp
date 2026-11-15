#include "TerrainBinding.h"
#include "TerrainStore.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/collisions/Terrain.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"
#include <memory>

namespace terrain
{
  void bindTerrainVolumes(const TerrainStore& store, const SceneManager& scenes)
  {
    for (const auto& [sceneUUID, scene] : scenes.getScenes())
    {
      for (const auto& object : scene->getObjectManager()->getAllObjects())
      {
        const auto component = object->getComponent<Terrain>(ComponentType::collider);
        if (!component)
        {
          continue;
        }

        const std::shared_ptr<const TerrainVolume> found = store.find(component->getTerrainUUID());
        if (component->volume() == found)
        {
          continue;
        }

        component->setVolume(found);
        component->invalidateBounds();
      }
    }
  }
}
