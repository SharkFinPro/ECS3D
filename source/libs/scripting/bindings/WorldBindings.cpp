#include "WorldBindings.h"
#include "BindingContext.h"
#include <assets/AssetRegistry.h>
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/Transform.h>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
  // Returned strings point into this buffer, valid until the next WorldBindings call on the same
  // thread. Scripts run synchronously on the server loop thread, so the managed caller marshals the
  // result to a C# string immediately (see World.cs) before the next binding call overwrites it.
  thread_local std::string s_returnBuffer;

  const char* store(std::string value)
  {
    s_returnBuffer = std::move(value);
    return s_returnBuffer.c_str();
  }

  // Parse an optional uuid argument; an empty/invalid string becomes the nil uuid, which the query
  // treats as "ignore nothing" (real object uuids are never nil).
  uuids::uuid parseIgnore(const char* uuid)
  {
    if (!uuid)
    {
      return {};
    }

    return uuids::uuid::from_string(std::string(uuid)).value_or(uuids::uuid{});
  }

  // Object::start() covers only its own components; a prefab instance is a whole subtree, and every node
  // needs live component state before physics/replication read it.
  void startSubtree(const Object& object)
  {
    object.start();

    for (const auto& child : object.getChildren())
    {
      startSubtree(*child);
    }
  }
}

WorldBindings WorldBindingsProvider::getBindings()
{
  return WorldBindings {
    .findObjectByName = &bindFindObjectByName,
    .getObjectName = &bindGetObjectName,
    .objectExists = &bindObjectExists,
    .getAllObjectUuids = &bindGetAllObjectUuids,
    .spawnObject = &bindSpawnObject,
    .destroyObject = &bindDestroyObject,
    .raycast = &bindRaycast,
    .overlapSphere = &bindOverlapSphere,
    .spawnPrefab = &bindSpawnPrefab
  };
}

const char* WorldBindingsProvider::bindFindObjectByName(const char* name)
{
  const auto objectManager = BindingContext::getObjectManager();
  if (!objectManager || !name)
  {
    return store("");
  }

  const std::string target(name);
  for (const auto& object : objectManager->getAllObjects())
  {
    if (object->getName() == target)
    {
      return store(uuids::to_string(object->getUUID()));
    }
  }

  return store("");
}

const char* WorldBindingsProvider::bindGetObjectName(const char* uuid)
{
  const auto objectManager = BindingContext::getObjectManager();
  if (!objectManager || !uuid)
  {
    return store("");
  }

  const auto parsed = uuids::uuid::from_string(std::string(uuid));
  if (!parsed.has_value())
  {
    return store("");
  }

  const auto object = objectManager->getObjectByUUID(parsed.value());
  if (!object)
  {
    return store("");
  }

  return store(object->getName());
}

bool WorldBindingsProvider::bindObjectExists(const char* uuid)
{
  const auto objectManager = BindingContext::getObjectManager();
  if (!objectManager || !uuid)
  {
    return false;
  }

  const auto parsed = uuids::uuid::from_string(std::string(uuid));
  if (!parsed.has_value())
  {
    return false;
  }

  return objectManager->getObjectByUUID(parsed.value()) != nullptr;
}

const char* WorldBindingsProvider::bindGetAllObjectUuids()
{
  const auto objectManager = BindingContext::getObjectManager();
  if (!objectManager)
  {
    return store("");
  }

  // UUIDs never contain commas, so a comma-delimited list marshals back cleanly (see World.cs).
  std::string result;
  for (const auto& object : objectManager->getAllObjects())
  {
    if (!result.empty())
    {
      result += ',';
    }
    result += uuids::to_string(object->getUUID());
  }

  return store(result);
}

const char* WorldBindingsProvider::bindSpawnObject(const char* name, const float x, const float y, const float z)
{
  const auto objectManager = BindingContext::getObjectManager();
  if (!objectManager)
  {
    return store("");
  }

  // The default Object ctor already attaches a Transform. addObject assigns the manager + a fresh uuid.
  auto object = std::make_shared<Object>(name ? std::string(name) : std::string("Object"));
  objectManager->addObject(object);

  // A script only runs while the scene is running, so the newborn must be started too (live component
  // state) before its transform is positioned — otherwise physics/replication would read stopped values.
  object->start();

  if (const auto transform = object->getComponent<Transform>(ComponentType::transform))
  {
    transform->setPosition({ x, y, z });
  }

  // Record it so ServerApp can broadcast the spawn after the tick (scripting can't reach the net layer).
  BindingContext::recordSpawn(object);

  return store(uuids::to_string(object->getUUID()));
}

const char* WorldBindingsProvider::bindSpawnPrefab(const char* prefabUuid, const float x, const float y, const float z)
{
  const auto objectManager = BindingContext::getObjectManager();
  const auto assetRegistry = BindingContext::getAssetRegistry();
  if (!objectManager || !assetRegistry || !prefabUuid)
  {
    return store("");
  }

  const auto parsed = uuids::uuid::from_string(std::string(prefabUuid));
  if (!parsed.has_value())
  {
    return store("");
  }

  // The body is carried inline on the asset record; null when the uuid isn't a prefab or the blob is bad.
  const auto body = assetRegistry->getPrefabBody(parsed.value());
  if (!body.is_object())
  {
    std::cerr << "[WorldBindings] No prefab body for " << prefabUuid << std::endl;
    return store("");
  }

  std::shared_ptr<Object> object;
  try
  {
    object = objectManager->instantiate(body);
  }
  catch (const std::exception& e)
  {
    // instantiate throws on a malformed body (e.g. a component type this build doesn't know). A bad prefab
    // must degrade to "no spawn", never take down the tick loop.
    std::cerr << "[WorldBindings] Failed to instantiate prefab " << prefabUuid << ": " << e.what() << std::endl;
    return store("");
  }

  // A script only runs while the scene is running, so the whole newborn subtree must be started (live
  // component state) before the root is positioned — otherwise physics/replication read stopped values.
  startSubtree(*object);

  if (const auto transform = object->getComponent<Transform>(ComponentType::transform))
  {
    transform->setPosition({ x, y, z });
  }

  // One recorded spawn: Object::pack carries the whole subtree, so the client splices in the same tree.
  BindingContext::recordSpawn(object);

  return store(uuids::to_string(object->getUUID()));
}

void WorldBindingsProvider::bindDestroyObject(const char* uuid)
{
  const auto objectManager = BindingContext::getObjectManager();
  if (!objectManager || !uuid)
  {
    return;
  }

  const auto parsed = uuids::uuid::from_string(std::string(uuid));
  if (!parsed.has_value())
  {
    return;
  }

  const auto object = objectManager->getObjectByUUID(parsed.value());
  if (!object)
  {
    return;
  }

  // Mark for deletion (never mutate the object list mid script iteration); ServerApp broadcasts the
  // destroy and calls deleteObjectsMarkedForDeletion after the tick.
  objectManager->removeObject(object);
  BindingContext::recordDestroy(parsed.value());
}

const char* WorldBindingsProvider::bindRaycast(const float ox, const float oy, const float oz,
                                               const float dx, const float dy, const float dz,
                                               const float maxDistance, const uint32_t layerMask,
                                               const char* ignoreUuid)
{
  const auto objectManager = BindingContext::getObjectManager();
  const auto raycast = BindingContext::getRaycast();
  if (!objectManager || !raycast)
  {
    return store("");
  }

  uuids::uuid hitObject;
  glm::vec3 hitPoint(0.0f);
  glm::vec3 hitNormal(0.0f);
  float hitDistance = 0.0f;

  if (!raycast(*objectManager, { ox, oy, oz }, { dx, dy, dz }, maxDistance, layerMask,
               parseIgnore(ignoreUuid), hitObject, hitPoint, hitNormal, hitDistance))
  {
    return store("");
  }

  // "uuid,dist,px,py,pz,nx,ny,nz" — uuids have no commas, so this parses cleanly on the managed side.
  std::string result = uuids::to_string(hitObject);
  for (const float value : { hitDistance, hitPoint.x, hitPoint.y, hitPoint.z,
                             hitNormal.x, hitNormal.y, hitNormal.z })
  {
    result += ',';
    result += std::to_string(value);
  }

  return store(result);
}

const char* WorldBindingsProvider::bindOverlapSphere(const float cx, const float cy, const float cz,
                                                     const float radius, const uint32_t layerMask,
                                                     const char* ignoreUuid)
{
  const auto objectManager = BindingContext::getObjectManager();
  const auto overlapSphere = BindingContext::getOverlapSphere();
  if (!objectManager || !overlapSphere)
  {
    return store("");
  }

  std::vector<uuids::uuid> results;
  overlapSphere(*objectManager, { cx, cy, cz }, radius, layerMask, parseIgnore(ignoreUuid), results);

  std::string out;
  for (const auto& uuid : results)
  {
    if (!out.empty())
    {
      out += ',';
    }
    out += uuids::to_string(uuid);
  }

  return store(out);
}
