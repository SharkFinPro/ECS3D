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

void ObjectManager::fixedUpdate(const float dt) const
{
  for (const auto& object : m_objects)
  {
    object->fixedUpdate(dt);
  }

  m_collisionManager->update();
}

void ObjectManager::variableUpdate()
{
  m_objectGUIManager->update();

  deleteObjectsMarkedForDeletion();

  m_objectGUIManager->displaySelectedObjectGui();

  for (const auto& object : m_objects)
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

  m_objects.push_back(object);

  m_objectGUIManager->addObject(object);
}

void ObjectManager::duplicateObject(const std::shared_ptr<Object>& object)
{
  auto objectData = object->serialize();
  objectData.at("name") = std::string(objectData.at("name")) + " - Copy";

  addObject(std::make_shared<Object>(objectData, this));
}

void ObjectManager::start() const
{
  for (const auto& object : m_objects)
  {
    object->start();
  }
}

void ObjectManager::stop() const
{
  for (const auto& object : m_objects)
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

void ObjectManager::deleteObjectsMarkedForDeletion()
{
  if (m_objectsToRemove.empty())
  {
    return;
  }

  for (const auto& object : m_objectsToRemove)
  {
    std::erase(m_objects, object);
  }

  m_objectsToRemove.clear();
}
