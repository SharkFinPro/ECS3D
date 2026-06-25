#include "Replication.h"
#include "ComponentRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "objects/components/Transform.h"
#include "objects/components/Script.h"
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

    // Send LOCAL transforms: the client rebuilds the world transform by walking parents itself, so a
    // parent-combined value would double-count under hierarchy.
    const auto position = transform->getLocalPosition();
    const auto rotation = transform->getLocalRotation();
    const auto scale = transform->getLocalScale();

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

    // The delta carries LOCAL transforms, so write them straight back as local values.
    const auto& position = entry.at("position");
    const auto& rotation = entry.at("rotation");
    const auto& scale = entry.at("scale");

    transform->setPosition(glm::vec3(position.at(0), position.at(1), position.at(2)));
    transform->setRotation(glm::vec3(rotation.at(0), rotation.at(1), rotation.at(2)));
    transform->setScale(glm::vec3(scale.at(0), scale.at(1), scale.at(2)));
  }
}

nlohmann::json buildComponentEdit(const uuids::uuid& objectUUID,
                                  const std::shared_ptr<Component>& component)
{
  auto data = component->serialize();

  nlohmann::json edit = {
    { "object", uuids::to_string(objectUUID) },
    { "type", data.at("type") },
    { "data", data }
  };

  // Scripts are keyed by class name (an object can hold several), so the apply side needs it to find
  // the right instance.
  if (const auto script = std::dynamic_pointer_cast<Script>(component))
  {
    edit["className"] = script->getClassName();
  }

  return edit;
}

void applyComponentEdit(const ObjectManager& objectManager, const nlohmann::json& edit)
{
  const auto parsed = uuids::uuid::from_string(std::string(edit.at("object")));
  if (!parsed.has_value())
  {
    return;
  }

  const auto object = objectManager.getObjectByUUID(parsed.value());
  if (!object)
  {
    return;
  }

  const std::string type = edit.at("type");
  const std::string className = edit.contains("className") ? edit.at("className") : "";
  const auto& data = edit.at("data");

  // Match the target by its serialize "type" (and class name for scripts), then reuse loadFromJSON.
  const auto matches = [&](const std::shared_ptr<Component>& component) {
    if (component->serialize().at("type") != type)
    {
      return false;
    }

    if (const auto script = std::dynamic_pointer_cast<Script>(component))
    {
      return script->getClassName() == className;
    }

    return true;
  };

  for (const auto& [componentType, component] : object->getComponents())
  {
    if (matches(component))
    {
      component->loadFromJSON(data);
      return;
    }
  }

  for (const auto& script : object->getScripts())
  {
    if (matches(script))
    {
      script->loadFromJSON(data);
      return;
    }
  }
}

nlohmann::json buildAddObject(const std::string& name, const uuids::uuid* parentUUID)
{
  nlohmann::json edit = {
    { "op", "addObject" },
    { "name", name }
  };

  if (parentUUID)
  {
    edit["parent"] = uuids::to_string(*parentUUID);
  }

  return edit;
}

nlohmann::json buildRemoveObject(const uuids::uuid& objectUUID)
{
  return {
    { "op", "removeObject" },
    { "object", uuids::to_string(objectUUID) }
  };
}

nlohmann::json buildAddComponent(const uuids::uuid& objectUUID, const std::string& componentKey)
{
  return {
    { "op", "addComponent" },
    { "object", uuids::to_string(objectUUID) },
    { "component", componentKey }
  };
}

nlohmann::json buildRemoveComponent(const uuids::uuid& objectUUID,
                                    const std::shared_ptr<Component>& component)
{
  nlohmann::json edit = {
    { "op", "removeComponent" },
    { "object", uuids::to_string(objectUUID) },
    { "type", component->serialize().at("type") }
  };

  if (const auto script = std::dynamic_pointer_cast<Script>(component))
  {
    edit["className"] = script->getClassName();
  }

  return edit;
}

void applySceneEdit(ObjectManager& objectManager, const nlohmann::json& edit)
{
  const std::string op = edit.at("op");

  if (op == "addObject")
  {
    const std::string name = edit.value("name", "Object");

    const auto object = std::make_shared<Object>(name);

    if (edit.contains("parent"))
    {
      if (const auto parsed = uuids::uuid::from_string(std::string(edit.at("parent"))))
      {
        if (const auto parent = objectManager.getObjectByUUID(parsed.value()))
        {
          object->setParent(parent);
        }
      }
    }

    objectManager.addObject(object);
    return;
  }

  // Every other op targets an existing object.
  const auto parsed = uuids::uuid::from_string(std::string(edit.at("object")));
  if (!parsed.has_value())
  {
    return;
  }

  const auto object = objectManager.getObjectByUUID(parsed.value());
  if (!object)
  {
    return;
  }

  if (op == "removeObject")
  {
    objectManager.removeObject(object);
    objectManager.deleteObjectsMarkedForDeletion();
    return;
  }

  if (op == "addComponent")
  {
    const std::string key = edit.at("component");

    if (const auto component = objectManager.getComponentRegistry()->create(key))
    {
      object->addComponent(component);
    }

    return;
  }

  if (op == "removeComponent")
  {
    const std::string type = edit.at("type");
    const std::string className = edit.contains("className") ? edit.at("className") : "";

    const auto matches = [&](const std::shared_ptr<Component>& component) {
      if (component->serialize().at("type") != type)
      {
        return false;
      }

      if (const auto script = std::dynamic_pointer_cast<Script>(component))
      {
        return script->getClassName() == className;
      }

      return true;
    };

    for (const auto& [componentType, component] : object->getComponents())
    {
      if (matches(component))
      {
        object->removeComponent(component);
        return;
      }
    }

    for (const auto& script : object->getScripts())
    {
      if (matches(script))
      {
        object->removeComponent(script);
        return;
      }
    }
  }
}

}
