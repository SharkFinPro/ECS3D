#include "TerrainStreamFocus.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/PlayerController.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/Terrain.h"
#include <glm/gtc/quaternion.hpp>
#include <memory>

namespace terrain
{
  glm::vec3 streamFocus(const ObjectManager& objects, const uuids::uuid& terrain, const glm::vec3 worldPoint,
                        const float voxelSize)
  {
    const Object* owner = nullptr;

    for (const auto& object : objects.getAllObjects())
    {
      const auto component = object->getComponent<Terrain>(ComponentType::collider);
      if (!component || component->getTerrainUUID() != terrain ||
          !object->getComponent<Transform>(ComponentType::transform))
      {
        continue;
      }

      if (owner == nullptr || object->getUUID() < owner->getUUID())
      {
        owner = object.get();
      }
    }

    if (owner == nullptr)
    {
      return worldPoint;
    }

    const auto transform = owner->getComponent<Transform>(ComponentType::transform);
    const glm::vec3 local = glm::inverse(transform->getOrientation()) * (worldPoint - transform->getPosition());

    return local / voxelSize;
  }

  std::optional<glm::vec3> playerPosition(const ObjectManager& objects, const int32_t slot)
  {
    const Object* found = nullptr;

    for (const auto& object : objects.getAllObjects())
    {
      const auto controller = object->getComponent<PlayerController>(ComponentType::playerController);
      if (!controller || controller->getPlayerSlot() != slot ||
          !object->getComponent<Transform>(ComponentType::transform))
      {
        continue;
      }

      if (found == nullptr || object->getUUID() < found->getUUID())
      {
        found = object.get();
      }
    }

    if (found == nullptr)
    {
      return std::nullopt;
    }

    return found->getComponent<Transform>(ComponentType::transform)->getPosition();
  }
}
