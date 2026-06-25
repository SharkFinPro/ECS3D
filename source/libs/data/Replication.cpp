#include "Replication.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "objects/components/Transform.h"
#include <nlohmann/json.hpp>

namespace replication {

nlohmann::json buildStateDelta(const ObjectManager& objectManager)
{
  nlohmann::json delta = nlohmann::json::array();

  for (const auto& object : objectManager.getAllObjects())
  {
    const auto transform = object->getComponent<Transform>(ComponentType::transform);

    if (!transform || transform->getOwner() != object.get())
    {
      continue;
    }

    const auto position = transform->getPosition();
    const auto rotation = transform->getRotation();
    const auto scale = transform->getScale();

    delta.push_back({
      { "uuid", uuids::to_string(object->getUUID()) },
      { "position", { position.x, position.y, position.z } },
      { "rotation", { rotation.x, rotation.y, rotation.z } },
      { "scale", { scale.x, scale.y, scale.z } }
    });
  }

  return delta;
}

void applyStateDelta(const ObjectManager& objectManager, const nlohmann::json& delta)
{
  for (const auto& entry : delta)
  {
    const auto parsed = uuids::uuid::from_string(std::string(entry.at("uuid")));
    if (!parsed.has_value())
    {
      continue;
    }

    const auto object = objectManager.getObjectByUUID(parsed.value());
    if (!object)
    {
      continue;
    }

    const auto transform = object->getComponent<Transform>(ComponentType::transform);
    if (!transform)
    {
      continue;
    }

    // TODO: a StateDelta should only need to move the object (set position/rotation/scale). The
    // TODO:   getPosition/Scale/Rotation values are world-space (parent-combined), so the client's
    // TODO:   apply needs to write LOCAL values — add setters or send local transforms instead.
    const auto& position = entry.at("position");
    const auto& rotation = entry.at("rotation");

    transform->setRotation(glm::vec3(rotation.at(0), rotation.at(1), rotation.at(2)));
    transform->move(glm::vec3(position.at(0), position.at(1), position.at(2)) - transform->getPosition());
  }
}

}
