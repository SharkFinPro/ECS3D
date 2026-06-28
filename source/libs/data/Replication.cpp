#include "Replication.h"
#include "ComponentRegistry.h"
#include "assets/AssetRegistry.h"
#include "scenes/SceneManager.h"
#include "scenes/SceneAsset.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "objects/components/Transform.h"
#include "objects/components/Script.h"
#include <Protocol.h>
#include <nlohmann/json.hpp>
#include <cmath>

namespace replication {

void packStateDelta(net::Message& message, const ObjectManager& objectManager)
{
  // Collect first so the entry count can lead (Message is append-only — there's no way to back-patch a
  // header once entries are written). Each entry is uuid + the three local transform vectors.
  struct Entry {
    std::string uuid;
    glm::vec3 position;
    glm::vec3 rotation;
    glm::vec3 scale;
  };
  std::vector<Entry> entries;

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

    // Skip NaN/inf rather than replicate bad data (the receiver would write it straight into the scene).
    auto finite3 = [](const glm::vec3& v) {
      return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    if (!finite3(position) || !finite3(rotation) || !finite3(scale))
    {
      continue;
    }

    entries.push_back({ uuids::to_string(object->getUUID()), position, rotation, scale });
  }

  message.write(static_cast<uint32_t>(entries.size()));
  for (const auto& entry : entries)
  {
    message.writeString(entry.uuid);
    message.write(entry.position);
    message.write(entry.rotation);
    message.write(entry.scale);
  }
}

void unpackStateDelta(const ObjectManager& objectManager, const net::Message& message)
{
  net::MessageReader reader(message);

  const uint32_t count = reader.read<uint32_t>();
  for (uint32_t i = 0; i < count; ++i)
  {
    const auto uuid = reader.readString();
    const auto position = reader.read<glm::vec3>();
    const auto rotation = reader.read<glm::vec3>();
    const auto scale = reader.read<glm::vec3>();

    const auto parsed = uuids::uuid::from_string(uuid);
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
    transform->setPosition(position);
    transform->setRotation(rotation);
    transform->setScale(scale);
  }
}

net::Message buildComponentEdit(const uuids::uuid& objectUUID,
                                const std::shared_ptr<Component>& component)
{
  net::Message message(net::MessageType::editComponent);

  message.writeString(uuids::to_string(objectUUID));
  component->pack(message);

  return message;
}

void applyComponentEdit(const ObjectManager& objectManager, const net::Message& edit)
{
  net::MessageReader reader(edit);
  const auto objectUUIDString = reader.readString();
  const auto objectUUID = uuids::uuid::from_string(objectUUIDString);
  if (!objectUUID.has_value())
  {
    return;
  }

  const auto object = objectManager.getObjectByUUID(objectUUID.value());
  if (!object)
  {
    return;
  }

  // Each component packs its type (or, for colliders, its subtype) first as a discriminator.
  const auto componentType = reader.read<ComponentType>();

  // Scripts live in their own list, keyed by class name, and pack the name next so the right one can be
  // found before unpack() reads the remaining field data.
  if (componentType == ComponentType::script)
  {
    const auto className = reader.readString();

    for (const auto& script : object->getScripts())
    {
      if (const auto scriptComponent = std::dynamic_pointer_cast<Script>(script);
          scriptComponent && scriptComponent->getClassName() == className)
      {
        script->unpack(reader);
        return;
      }
    }

    return;
  }

  // Colliders pack their subtype as the discriminator, but the component map is keyed by the parent
  // (collider) type, so map back before looking it up.
  auto lookupType = componentType;
  if (const auto parent = subComponentTypeToParent.find(componentType); parent != subComponentTypeToParent.end())
  {
    lookupType = parent->second;
  }

  const auto components = object->getComponents();
  if (components.contains(lookupType))
  {
    components.at(lookupType)->unpack(reader);
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

nlohmann::json buildDuplicateObject(const uuids::uuid& objectUUID)
{
  return {
    { "op", "duplicateObject" },
    { "object", uuids::to_string(objectUUID) }
  };
}

nlohmann::json buildRenameObject(const uuids::uuid& objectUUID, const std::string& name)
{
  return {
    { "op", "renameObject" },
    { "object", uuids::to_string(objectUUID) },
    { "name", name }
  };
}

nlohmann::json buildAddScript(const uuids::uuid& objectUUID, const std::string& className)
{
  return {
    { "op", "addComponent" },
    { "object", uuids::to_string(objectUUID) },
    { "component", "Script" },
    { "className", className }
  };
}

nlohmann::json buildReparentObject(const uuids::uuid& objectUUID, const uuids::uuid* parentUUID)
{
  nlohmann::json edit = {
    { "op", "reparentObject" },
    { "object", uuids::to_string(objectUUID) }
  };

  if (parentUUID)
  {
    edit["parent"] = uuids::to_string(*parentUUID);
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

  if (op == "renameObject")
  {
    object->setName(edit.at("name"));
    return;
  }

  if (op == "duplicateObject")
  {
    objectManager.duplicateObject(object);
    return;
  }

  if (op == "reparentObject")
  {
    std::shared_ptr<Object> parent;
    if (edit.contains("parent"))
    {
      if (const auto parsedParent = uuids::uuid::from_string(std::string(edit.at("parent"))))
      {
        parent = objectManager.getObjectByUUID(parsedParent.value());
      }
    }

    // Don't create a cycle (drop onto self or a descendant).
    if (object == parent || (parent && object->isAncestorOf(parent)))
    {
      return;
    }

    if (const auto oldParent = object->getParent())
    {
      oldParent->removeChild(object);
    }
    else
    {
      objectManager.removeObjectFromRoot(object);
    }

    object->setParent(parent);

    if (parent)
    {
      parent->addChild(object);
    }
    else
    {
      objectManager.addObjectToRoot(object);
    }

    return;
  }

  if (op == "addComponent")
  {
    const std::string key = edit.at("component");

    if (const auto component = objectManager.getComponentRegistry()->create(key))
    {
      if (edit.contains("className"))
      {
        if (const auto script = std::dynamic_pointer_cast<Script>(component))
        {
          script->setClassName(edit.at("className"));
        }
      }

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

void applyAddAsset(AssetRegistry& assetRegistry,
                   SceneManager& sceneManager,
                   const std::shared_ptr<ComponentRegistry>& componentRegistry,
                   const nlohmann::json& asset)
{
  const auto parsed = uuids::uuid::from_string(asset.value("uuid", std::string{}));
  if (!parsed.has_value())
  {
    return;
  }

  const auto uuid = parsed.value();
  const std::string type = asset.value("assetType", std::string{});

  if (type == "model")
  {
    assetRegistry.registerAsset({ .uuid = uuid, .type = AssetType::Model, .path = asset.value("path", std::string{}) });
  }
  else if (type == "texture")
  {
    assetRegistry.registerAsset({ .uuid = uuid, .type = AssetType::Texture, .path = asset.value("path", std::string{}) });
  }
  else if (type == "script")
  {
    assetRegistry.registerAsset({ .uuid = uuid, .type = AssetType::Script,
      .path = asset.value("path", std::string{}), .className = asset.value("className", std::string{}) });
  }
  else if (type == "scene")
  {
    const std::string name = asset.value("name", std::string{ "New Scene" });
    sceneManager.addScene(std::make_shared<SceneAsset>(uuid, name, componentRegistry));
    // Also register it as an asset so it shows in the browser (double-click to load).
    assetRegistry.registerAsset({ .uuid = uuid, .type = AssetType::Scene, .path = name });
  }
}

}
