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
#include "WireTypes.h"
#include <Protocol.h>
#include <nlohmann/json.hpp>
#include <cmath>
#include <exception>
#include <new>

namespace replication {

void packStateDelta(net::Message& message, const ObjectManager& objectManager)
{
  // Collect first so the entry count can lead (Message is append-only - there's no way to back-patch a
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

ComponentEditResult applyComponentEdit(const ObjectManager& objectManager, const net::Message& edit)
{
  net::MessageReader reader(edit);

  // A reader that runs off the end of the payload throws, as does a script's field blob failing to parse.
  // The client and editor run loops have no guard of their own, so an escaping exception ends the
  // process; the authority's loop catches, but only after losing the edit.
  bool unpacking = false;

  try
  {
    const auto objectUUIDString = reader.readString();
    const auto objectUUID = uuids::uuid::from_string(objectUUIDString);
    if (!objectUUID.has_value())
    {
      return ComponentEditResult::malformedPayload;
    }

    const auto object = objectManager.getObjectByUUID(objectUUID.value());
    if (!object)
    {
      return ComponentEditResult::unknownObject;
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
          unpacking = true;
          script->unpack(reader);

          return ComponentEditResult::applied;
        }
      }

      return ComponentEditResult::unknownComponent;
    }

    // Colliders pack their subtype as the discriminator, but the component map is keyed by the parent
    // (collider) type, so map back before looking it up.
    auto lookupType = componentType;
    if (const auto parent = subComponentTypeToParent.find(componentType); parent != subComponentTypeToParent.end())
    {
      lookupType = parent->second;
    }

    const auto& components = object->getComponents();
    if (!components.contains(lookupType))
    {
      return ComponentEditResult::unknownComponent;
    }

    unpacking = true;
    components.at(lookupType)->unpack(reader);

    return ComponentEditResult::applied;
  }
  catch (const std::bad_alloc&)
  {
    // Out of memory is not a malformed payload, and pretending otherwise would send the caller looking
    // for a wire bug.
    throw;
  }
  catch (const std::exception&)
  {
    return unpacking ? ComponentEditResult::partiallyApplied : ComponentEditResult::malformedPayload;
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

nlohmann::json buildInstantiatePrefab(const uuids::uuid& prefabUUID)
{
  return {
    { "op", "instantiatePrefab" },
    { "prefab", uuids::to_string(prefabUUID) }
  };
}

namespace {
  SceneEditResult applyStructuralEdit(ObjectManager& objectManager, const nlohmann::json& edit,
                                      const AssetRegistry* assetRegistry)
  {
    const std::string op = edit.at("op");

    if (op == "instantiatePrefab")
    {
      // The only op keyed by an asset rather than an existing object: pull the prefab's body from the
      // registry and clone it in with fresh uuids.
      if (!assetRegistry)
      {
        return SceneEditResult::unknownAsset;
      }

      const auto parsedPrefab = uuids::uuid::from_string(std::string(edit.at("prefab")));
      if (!parsedPrefab.has_value())
      {
        return SceneEditResult::malformedEdit;
      }

      const auto body = assetRegistry->getPrefabBody(parsedPrefab.value());
      if (!body.is_object())
      {
        return SceneEditResult::unknownAsset;
      }

      // Guarded here rather than left to the catch below: the body belongs to the asset, not to the
      // edit, so a prefab whose stored blob is broken is a failure to apply and not a malformed edit -
      // reporting it as one sends whoever reads the log to look at the wire.
      try
      {
        objectManager.instantiate(body);
      }
      catch (const std::bad_alloc&)
      {
        throw;
      }
      catch (const std::exception&)
      {
        return SceneEditResult::failed;
      }

      return SceneEditResult::applied;
    }

    if (op == "addObject")
    {
      const std::string name = edit.value("name", "Object");

      const auto object = std::make_shared<Object>(name);

      // A parent that is named and does not resolve is not the same as no parent named at all: the
      // sender asked for a child of something, and rooting the object instead and calling it applied
      // reports the wrong answer for a view that is a round trip behind the authority.
      if (edit.contains("parent"))
      {
        const auto parsed = uuids::uuid::from_string(std::string(edit.at("parent")));
        if (!parsed.has_value())
        {
          return SceneEditResult::malformedEdit;
        }

        const auto parent = objectManager.getObjectByUUID(parsed.value());
        if (!parent)
        {
          return SceneEditResult::unknownObject;
        }

        object->setParent(parent);
      }

      objectManager.addObject(object);
      return SceneEditResult::applied;
    }

    // Every other op targets an existing object.
    const auto parsed = uuids::uuid::from_string(std::string(edit.at("object")));
    if (!parsed.has_value())
    {
      return SceneEditResult::malformedEdit;
    }

    const auto object = objectManager.getObjectByUUID(parsed.value());
    if (!object)
    {
      return SceneEditResult::unknownObject;
    }

    if (op == "removeObject")
    {
      objectManager.removeObject(object);
      objectManager.deleteObjectsMarkedForDeletion();
      return SceneEditResult::applied;
    }

    if (op == "renameObject")
    {
      object->setName(edit.at("name"));
      return SceneEditResult::applied;
    }

    if (op == "duplicateObject")
    {
      // Same reasoning as the prefab body: this re-parses the object's own serialization, so a failure
      // is the scene's and not the edit's.
      try
      {
        objectManager.duplicateObject(object);
      }
      catch (const std::bad_alloc&)
      {
        throw;
      }
      catch (const std::exception&)
      {
        return SceneEditResult::failed;
      }

      return SceneEditResult::applied;
    }

    if (op == "reparentObject")
    {
      // No parent named means "move to the scene root", which is a real request. A parent that is named
      // and does not resolve is a stale view, and falling through to a null parent would silently
      // detach the object from the parent it has instead - and report that as applied.
      std::shared_ptr<Object> parent;
      if (edit.contains("parent"))
      {
        const auto parsedParent = uuids::uuid::from_string(std::string(edit.at("parent")));
        if (!parsedParent.has_value())
        {
          return SceneEditResult::malformedEdit;
        }

        parent = objectManager.getObjectByUUID(parsedParent.value());
        if (!parent)
        {
          return SceneEditResult::unknownObject;
        }
      }

      // Don't create a cycle (drop onto self or a descendant).
      if (object == parent || (parent && object->isAncestorOf(parent)))
      {
        return SceneEditResult::rejected;
      }

      // Dropping an object back onto the parent it already has would only move it to the end of the
      // sibling list and cost a re-snapshot.
      if (object->getParent() == parent)
      {
        return SceneEditResult::rejected;
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

      return SceneEditResult::applied;
    }

    if (op == "addComponent")
    {
      const std::string key = edit.at("component");

      const auto component = objectManager.getComponentRegistry()->create(key);
      if (!component)
      {
        return SceneEditResult::unknownComponent;
      }

      if (edit.contains("className"))
      {
        if (const auto script = std::dynamic_pointer_cast<Script>(component))
        {
          script->setClassName(edit.at("className"));
        }
      }

      object->addComponent(component);

      return SceneEditResult::applied;
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
          return SceneEditResult::applied;
        }
      }

      for (const auto& script : object->getScripts())
      {
        if (matches(script))
        {
          object->removeComponent(script);
          return SceneEditResult::applied;
        }
      }

      return SceneEditResult::unknownComponent;
    }

    return SceneEditResult::malformedEdit;
  }
}

SceneEditResult applySceneEdit(ObjectManager& objectManager, const nlohmann::json& edit,
                               const AssetRegistry* assetRegistry)
{
  // Every op reaches its fields with at(), and instantiatePrefab builds a whole subtree from a body that
  // may name a component this build does not know. Without this the authority's only guard is the run
  // loop's catch, which logs the throw as a generic bad message and tells the caller nothing.
  try
  {
    return applyStructuralEdit(objectManager, edit, assetRegistry);
  }
  catch (const std::bad_alloc&)
  {
    // Out of memory is not a malformed edit, and pretending otherwise would send the caller looking for
    // a wire bug.
    throw;
  }
  catch (const nlohmann::json::exception&)
  {
    // A field the op needs is missing, or is not the type it is read as.
    return SceneEditResult::malformedEdit;
  }
  catch (const std::exception&)
  {
    return SceneEditResult::failed;
  }
}

net::Message buildObjectSpawned(const Object& object)
{
  net::Message message(net::MessageType::objectSpawned);
  object.pack(message);
  return message;
}

net::Message buildObjectDestroyed(const uuids::uuid& objectUUID)
{
  net::Message message(net::MessageType::objectDestroyed);
  message.writeString(uuids::to_string(objectUUID));
  return message;
}

void applyObjectSpawned(ObjectManager& objectManager, const net::Message& message)
{
  // Symmetric with buildObjectSpawned: reconstruct one root object exactly as ObjectManager::unpack does
  // per object (fresh Object, registered so it has a manager, then unpacked from the packed blob).
  net::MessageReader reader(message);

  auto object = std::make_shared<Object>();
  objectManager.addObject(object);

  try
  {
    object->unpack(reader);
  }
  catch (...)
  {
    // unpack registers each node with the manager before reading it, so a payload that runs out part way
    // through would otherwise strand a half-built subtree in the live scene.
    objectManager.discardSubtree(object);
    throw;
  }
}

void applyObjectDestroyed(ObjectManager& objectManager, const net::Message& message)
{
  net::MessageReader reader(message);

  const auto parsed = uuids::uuid::from_string(reader.readString());
  if (!parsed.has_value())
  {
    return;
  }

  const auto object = objectManager.getObjectByUUID(parsed.value());
  if (!object)
  {
    return;
  }

  objectManager.removeObject(object);
  objectManager.deleteObjectsMarkedForDeletion();
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
  else if (type == "prefab")
  {
    // Keyed by display name (like a scene) and carrying its body inline. Re-registering an existing name
    // updates that prefab's body in place, so "Save as Prefab" over an existing name means "update it".
    assetRegistry.registerAsset({ .uuid = uuid, .type = AssetType::Prefab,
      .path = asset.value("name", std::string{}), .body = asset.value("body", std::string{}) });
  }
  else if (type == "scene")
  {
    const std::string name = asset.value("name", std::string{ "New Scene" });
    sceneManager.addScene(std::make_shared<SceneAsset>(uuid, name, componentRegistry));
    // Also register it as an asset so it shows in the browser (double-click to load).
    assetRegistry.registerAsset({ .uuid = uuid, .type = AssetType::Scene, .path = name });
  }
}

net::Message packAddAsset(const nlohmann::json& asset)
{
  net::Message message(net::MessageType::addAsset);

  message.writeString(asset.value("assetType", std::string{}));
  message.writeString(asset.value("uuid", std::string{}));
  message.writeString(asset.value("path", std::string{}));
  message.writeString(asset.value("name", std::string{}));
  message.writeString(asset.value("className", std::string{}));
  message.writeString(asset.value("body", std::string{}));

  return message;
}

nlohmann::json unpackAddAsset(const net::Message& message)
{
  net::MessageReader reader(message);

  nlohmann::json asset;
  asset["assetType"] = reader.readString();
  asset["uuid"] = reader.readString();
  asset["path"] = reader.readString();
  asset["name"] = reader.readString();
  asset["className"] = reader.readString();
  asset["body"] = reader.readString();

  return asset;
}

nlohmann::json buildRenameAsset(const uuids::uuid& assetUUID, const std::string& displayName)
{
  return {
    { "uuid", uuids::to_string(assetUUID) },
    { "displayName", displayName }
  };
}

nlohmann::json buildRemoveAsset(const uuids::uuid& assetUUID)
{
  return {
    { "uuid", uuids::to_string(assetUUID) }
  };
}

void applyRenameAsset(AssetRegistry& assetRegistry, const nlohmann::json& op)
{
  const auto parsed = uuids::uuid::from_string(op.value("uuid", std::string{}));
  if (!parsed.has_value())
  {
    return;
  }

  assetRegistry.renameAsset(parsed.value(), op.value("displayName", std::string{}));
}

void applyRemoveAsset(AssetRegistry& assetRegistry, const nlohmann::json& op)
{
  const auto parsed = uuids::uuid::from_string(op.value("uuid", std::string{}));
  if (!parsed.has_value())
  {
    return;
  }

  assetRegistry.removeAsset(parsed.value());
}

net::Message packRenameAsset(const nlohmann::json& op)
{
  net::Message message(net::MessageType::renameAsset);

  message.writeString(op.value("uuid", std::string{}));
  message.writeString(op.value("displayName", std::string{}));

  return message;
}

nlohmann::json unpackRenameAsset(const net::Message& message)
{
  net::MessageReader reader(message);

  nlohmann::json op;
  op["uuid"] = reader.readString();
  op["displayName"] = reader.readString();

  return op;
}

net::Message packRemoveAsset(const nlohmann::json& op)
{
  net::Message message(net::MessageType::removeAsset);

  message.writeString(op.value("uuid", std::string{}));

  return message;
}

nlohmann::json unpackRemoveAsset(const net::Message& message)
{
  net::MessageReader reader(message);

  nlohmann::json op;
  op["uuid"] = reader.readString();

  return op;
}

}
