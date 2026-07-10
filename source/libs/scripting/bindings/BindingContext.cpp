#include "BindingContext.h"
#include <utility>

ObjectManager* BindingContext::s_objectManager = nullptr;
AssetRegistry* BindingContext::s_assetRegistry = nullptr;
std::vector<std::shared_ptr<Object>> BindingContext::s_spawned;
std::vector<uuids::uuid> BindingContext::s_destroyed;
BindingContext::RaycastFn BindingContext::s_raycast = nullptr;
BindingContext::OverlapSphereFn BindingContext::s_overlapSphere = nullptr;

void BindingContext::setObjectManager(ObjectManager* objectManager)
{
  s_objectManager = objectManager;
}

ObjectManager* BindingContext::getObjectManager()
{
  return s_objectManager;
}

void BindingContext::setAssetRegistry(AssetRegistry* assetRegistry)
{
  s_assetRegistry = assetRegistry;
}

AssetRegistry* BindingContext::getAssetRegistry()
{
  return s_assetRegistry;
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

void BindingContext::setRaycast(const RaycastFn raycast)
{
  s_raycast = raycast;
}

BindingContext::RaycastFn BindingContext::getRaycast()
{
  return s_raycast;
}

void BindingContext::setOverlapSphere(const OverlapSphereFn overlapSphere)
{
  s_overlapSphere = overlapSphere;
}

BindingContext::OverlapSphereFn BindingContext::getOverlapSphere()
{
  return s_overlapSphere;
}
