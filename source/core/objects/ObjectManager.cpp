#include "ObjectManager.h"
#include "Object.h"
#include "CollisionManager.h"
#include "ObjectGUIManager.h"
#include <nlohmann/json.hpp>

ObjectManager::ObjectManager(ECS3D* ecs)
  : m_ecs(ecs),
    m_collisionManager(std::make_shared<CollisionManager>()),
    m_objectGUIManager(std::make_shared<ObjectGUIManager>(this))
{}

void ObjectManager::updateGui() const
{
  m_objectGUIManager->update();

  m_objectGUIManager->displaySelectedObjectGui();
}

void ObjectManager::fixedUpdate(const float dt) const
{
  for (const auto& object : m_allObjects)
  {
    object->fixedUpdate(dt);
  }

  m_collisionManager->update();
}

void ObjectManager::variableUpdate()
{
  deleteObjectsMarkedForDeletion();

  for (const auto& object : m_allObjects)
  {
    object->variableUpdate();
  }

  #ifdef COLLISION_DEBUG
    m_collisionManager->variableUpdate();
  #endif
}

ECS3D* ObjectManager::getECS() const
{
  return m_ecs;
}

std::shared_ptr<CollisionManager> ObjectManager::getCollisionManager() const
{
  return m_collisionManager;
}

void ObjectManager::addObject(const std::shared_ptr<Object>& object)
{
  object->setManager(this);

  m_collisionManager->addObject(object);

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

void ObjectManager::duplicateObject(const std::shared_ptr<Object>& object)
{
  auto objectData = object->serialize();
  objectData.at("name") = std::string(objectData.at("name")) + " - Copy";

  const auto newObject = std::make_shared<Object>(objectData, this);
  newObject->setParent(object->getParent());

  addObject(newObject);
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

void ObjectManager::removeObject(const std::shared_ptr<Object>& object)
{
  m_objectsToRemove.push_back(object);
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

    m_collisionManager->removeObject(object);

    std::erase(m_allObjects, object);
  }

  m_objectsToRemove.clear();
}
