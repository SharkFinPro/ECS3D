#include "ObjectManager.h"
#include "Object.h"
#include <nlohmann/json.hpp>
#include <Protocol.h>
#include <algorithm>
#include <array>
#include <functional>

ObjectManager::ObjectManager(std::shared_ptr<ComponentRegistry> componentRegistry)
  : m_componentRegistry(std::move(componentRegistry)),
    m_rng([] {
      std::random_device rd;
      auto seed_data = std::array<int, std::mt19937::state_size>{};
      std::ranges::generate(seed_data, std::ref(rd));
      std::seed_seq seq(std::begin(seed_data), std::end(seed_data));
      return std::mt19937(seq);
    }()),
    m_uuidGenerator(m_rng)
{}

std::shared_ptr<ComponentRegistry> ObjectManager::getComponentRegistry() const
{
  return m_componentRegistry;
}

uuids::uuid ObjectManager::createUUID()
{
  return m_uuidGenerator();
}

void ObjectManager::addObject(const std::shared_ptr<Object>& object)
{
  object->setManager(this);

  // Colliders aren't registered here: CollisionSystem discovers them by scanning the objects each tick.

  m_allObjects.push_back(object);

  if (object->getParent() == nullptr)
  {
    m_objects.push_back(object);
  }
  else
  {
    object->getParent()->addChild(object);
  }
}

void ObjectManager::addObjectToRoot(const std::shared_ptr<Object>& object)
{
  m_objects.push_back(object);
}

void ObjectManager::removeObjectFromRoot(const std::shared_ptr<Object>& object)
{
  std::erase(m_objects, object);
}

void ObjectManager::reassignUUIDs(nlohmann::json& objectData)
{
  objectData["uuid"] = uuids::to_string(createUUID());

  if (objectData.contains("children"))
  {
    for (auto& child : objectData.at("children"))
    {
      reassignUUIDs(child);
    }
  }
}

std::shared_ptr<Object> ObjectManager::instantiateUnder(const nlohmann::json& objectData,
                                                       const std::shared_ptr<Object>& parent)
{
  // Work on a copy: reassignUUIDs rewrites the blob, and a prefab body is reused across instantiations.
  auto data = objectData;

  // Give the new object (and every descendant) fresh uuids - reusing the originals would collide in the
  // uuid-keyed replication/picking.
  reassignUUIDs(data);

  const auto newObject = std::make_shared<Object>(data, this);
  newObject->setParent(parent);

  addObject(newObject);

  // Object's ctor loads only its own components/scripts; children are separate objects, so build them.
  if (data.contains("children"))
  {
    newObject->loadChildren(data.at("children"));
  }

  return newObject;
}

std::shared_ptr<Object> ObjectManager::instantiate(const nlohmann::json& objectData)
{
  return instantiateUnder(objectData, nullptr);
}

void ObjectManager::duplicateObject(const std::shared_ptr<Object>& object)
{
  auto objectData = object->serialize();
  objectData["name"] = std::string(objectData.at("name")) + " - Copy";

  // A duplicate sits beside its original; a prefab instance (instantiate) lands at the scene root.
  instantiateUnder(objectData, object->getParent());
}

void ObjectManager::start() const
{
  for (const auto& object : m_allObjects)
  {
    object->start();
  }
}

void ObjectManager::stop() const
{
  for (const auto& object : m_allObjects)
  {
    object->stop();
  }
}

nlohmann::json ObjectManager::serialize() const
{
  nlohmann::json data = {
    { "objects", nlohmann::json::array() }
  };

  for (const auto& object : m_objects)
  {
    data["objects"].push_back(object->serialize());
  }

  return data;
}

void ObjectManager::pack(net::Message& message) const
{
  // Mirrors serialize(): only the root objects are written, each packing its own subtree recursively.
  message.write(static_cast<uint32_t>(m_objects.size()));

  for (const auto& object : m_objects)
  {
    object->pack(message);
  }
}

void ObjectManager::unpack(net::MessageReader& messageReader)
{
  const uint32_t objectCount = messageReader.read<uint32_t>();

  for (uint32_t i = 0; i < objectCount; ++i)
  {
    auto object = std::make_shared<Object>();
    addObject(object);

    object->unpack(messageReader);
  }
}

void ObjectManager::removeObject(const std::shared_ptr<Object>& object)
{
  m_objectsToRemove.push_back(object);
}

void ObjectManager::deleteObjectsMarkedForDeletion()
{
  if (m_objectsToRemove.empty())
  {
    return;
  }

  for (const auto& object : m_objectsToRemove)
  {
    const auto parent = object->getParent();
    if (parent)
    {
      parent->removeChild(object);
    }
    else
    {
      removeObjectFromRoot(object);
    }

    const auto children = object->getChildren();
    for (const auto& child : children)
    {
      object->removeChild(child);
      child->setParent(parent);

      if (parent)
      {
        parent->addChild(child);
      }
      else
      {
        addObjectToRoot(child);
      }
    }

    // (No CollisionSystem deregistration needed: it rescans the live objects each tick, so a removed
    // object naturally drops out.)

    std::erase(m_allObjects, object);
  }

  m_objectsToRemove.clear();
}

std::shared_ptr<Object> ObjectManager::getObjectByUUID(const uuids::uuid uuid) const
{
  for (const auto& object : m_allObjects)
  {
    if (object->getUUID() == uuid)
    {
      return object;
    }
  }

  return nullptr;
}

const std::vector<std::shared_ptr<Object>>& ObjectManager::getObjects() const
{
  return m_objects;
}

const std::vector<std::shared_ptr<Object>>& ObjectManager::getAllObjects() const
{
  return m_allObjects;
}
