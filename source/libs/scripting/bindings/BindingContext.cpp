#include "BindingContext.h"
#include <utility>

ObjectManager* BindingContext::s_objectManager = nullptr;
std::vector<std::shared_ptr<Object>> BindingContext::s_spawned;
std::vector<uuids::uuid> BindingContext::s_destroyed;

void BindingContext::setObjectManager(ObjectManager* objectManager)
{
  s_objectManager = objectManager;
}

ObjectManager* BindingContext::getObjectManager()
{
  return s_objectManager;
}

void BindingContext::recordSpawn(const std::shared_ptr<Object>& object)
{
  s_spawned.push_back(object);
}

void BindingContext::recordDestroy(const uuids::uuid& objectUUID)
{
  s_destroyed.push_back(objectUUID);
}

std::vector<std::shared_ptr<Object>> BindingContext::takeSpawned()
{
  return std::exchange(s_spawned, {});
}

std::vector<uuids::uuid> BindingContext::takeDestroyed()
{
  return std::exchange(s_destroyed, {});
}
