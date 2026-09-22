#include "ColliderBindings.h"
#include "BindingContext.h"
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/collisions/BoxCollider.h>
#include <objects/components/collisions/Collider.h>
#include <objects/components/collisions/SphereCollider.h>
#include <memory>
#include <string>

namespace {
  // Also hands back the parsed object uuid, so a setter that needs to record a replicated edit doesn't
  // have to re-parse the string it just resolved.
  std::shared_ptr<Collider> find(const char* uuid, uuids::uuid* outObjectUUID = nullptr)
  {
    const auto objectManager = BindingContext::getObjectManager();
    if (!objectManager || !uuid)
    {
      return nullptr;
    }

    const auto parsed = uuids::uuid::from_string(std::string(uuid));
    if (!parsed.has_value())
    {
      return nullptr;
    }

    const auto object = objectManager->getObjectByUUID(parsed.value());
    if (!object)
    {
      return nullptr;
    }

    const auto collider = object->getComponent<Collider>(ComponentType::collider);
    if (collider && outObjectUUID)
    {
      *outObjectUUID = parsed.value();
    }

    return collider;
  }

  // Narrows to the concrete shape, failing (nullptr) when the collider is some other shape - the fail-safe
  // half of the box/sphere-only accessors.
  std::shared_ptr<BoxCollider> findBox(const char* uuid, uuids::uuid* outObjectUUID = nullptr)
  {
    return std::dynamic_pointer_cast<BoxCollider>(find(uuid, outObjectUUID));
  }

  std::shared_ptr<SphereCollider> findSphere(const char* uuid, uuids::uuid* outObjectUUID = nullptr)
  {
    return std::dynamic_pointer_cast<SphereCollider>(find(uuid, outObjectUUID));
  }
}

ColliderBindings ColliderBindingsProvider::getBindings()
{
  return ColliderBindings {
    .getShape = &bindGetShape,
    .getIsTrigger = &bindGetIsTrigger,
    .setIsTrigger = &bindSetIsTrigger,
    .getLayer = &bindGetLayer,
    .setLayer = &bindSetLayer,
    .getMask = &bindGetMask,
    .setMask = &bindSetMask,
    .getBoxOffset = &bindGetBoxOffset,
    .setBoxOffset = &bindSetBoxOffset,
    .getBoxSize = &bindGetBoxSize,
    .setBoxSize = &bindSetBoxSize,
    .getSphereOffset = &bindGetSphereOffset,
    .setSphereOffset = &bindSetSphereOffset,
    .getSphereRadius = &bindGetSphereRadius,
    .setSphereRadius = &bindSetSphereRadius,
    .has = &bindHas
  };
}

int ColliderBindingsProvider::bindGetShape(const char* uuid)
{
  const auto collider = find(uuid);
  if (!collider)
  {
    return 0; // none
  }

  return collider->getColliderType() == ColliderType::boxCollider ? 1 : 2;
}

bool ColliderBindingsProvider::bindGetIsTrigger(const char* uuid)
{
  const auto collider = find(uuid);
  return collider && collider->isTrigger();
}

bool ColliderBindingsProvider::bindSetIsTrigger(const char* uuid, const bool isTrigger)
{
  uuids::uuid objectUUID;
  const auto collider = find(uuid, &objectUUID);
  if (!collider)
  {
    return false;
  }

  collider->setIsTrigger(isTrigger);

  // Not covered by the per-tick state delta (Transform only), so replicate it like the editor's own
  // component edits: buffer it here (scripting can't reach the net layer) for the app to broadcast.
  BindingContext::recordComponentEdit(objectUUID, collider);

  return true;
}

uint32_t ColliderBindingsProvider::bindGetLayer(const char* uuid)
{
  const auto collider = find(uuid);
  return collider ? collider->getLayer() : 0u;
}

bool ColliderBindingsProvider::bindSetLayer(const char* uuid, const uint32_t layer)
{
  uuids::uuid objectUUID;
  const auto collider = find(uuid, &objectUUID);
  if (!collider)
  {
    return false;
  }

  collider->setLayer(layer); // clamped to 0-31 by the setter

  BindingContext::recordComponentEdit(objectUUID, collider);

  return true;
}

uint32_t ColliderBindingsProvider::bindGetMask(const char* uuid)
{
  const auto collider = find(uuid);
  return collider ? collider->getMask() : 0u;
}

bool ColliderBindingsProvider::bindSetMask(const char* uuid, const uint32_t mask)
{
  uuids::uuid objectUUID;
  const auto collider = find(uuid, &objectUUID);
  if (!collider)
  {
    return false;
  }

  collider->setMask(mask);

  BindingContext::recordComponentEdit(objectUUID, collider);

  return true;
}

bool ColliderBindingsProvider::bindGetBoxOffset(const char* uuid, float* x, float* y, float* z)
{
  const auto box = findBox(uuid);
  if (!box)
  {
    return false;
  }

  const auto offset = box->getLocalPosition();
  *x = offset.x;
  *y = offset.y;
  *z = offset.z;

  return true;
}

bool ColliderBindingsProvider::bindSetBoxOffset(const char* uuid, const float x, const float y, const float z)
{
  uuids::uuid objectUUID;
  const auto box = findBox(uuid, &objectUUID);
  if (!box)
  {
    return false;
  }

  // Non-finite input is dropped by the setter itself (BoxCollider::setPosition), not rechecked here.
  box->setPosition({ x, y, z });

  BindingContext::recordComponentEdit(objectUUID, box);

  return true;
}

bool ColliderBindingsProvider::bindGetBoxSize(const char* uuid, float* x, float* y, float* z)
{
  const auto box = findBox(uuid);
  if (!box)
  {
    return false;
  }

  const auto size = box->getLocalScale();
  *x = size.x;
  *y = size.y;
  *z = size.z;

  return true;
}

bool ColliderBindingsProvider::bindSetBoxSize(const char* uuid, const float x, const float y, const float z)
{
  uuids::uuid objectUUID;
  const auto box = findBox(uuid, &objectUUID);
  if (!box)
  {
    return false;
  }

  box->setScale({ x, y, z });

  BindingContext::recordComponentEdit(objectUUID, box);

  return true;
}

bool ColliderBindingsProvider::bindGetSphereOffset(const char* uuid, float* x, float* y, float* z)
{
  const auto sphere = findSphere(uuid);
  if (!sphere)
  {
    return false;
  }

  const auto offset = sphere->getLocalPosition();
  *x = offset.x;
  *y = offset.y;
  *z = offset.z;

  return true;
}

bool ColliderBindingsProvider::bindSetSphereOffset(const char* uuid, const float x, const float y, const float z)
{
  uuids::uuid objectUUID;
  const auto sphere = findSphere(uuid, &objectUUID);
  if (!sphere)
  {
    return false;
  }

  sphere->setPosition({ x, y, z });

  BindingContext::recordComponentEdit(objectUUID, sphere);

  return true;
}

bool ColliderBindingsProvider::bindGetSphereRadius(const char* uuid, float* radius)
{
  const auto sphere = findSphere(uuid);
  if (!sphere)
  {
    return false;
  }

  *radius = sphere->getLocalRadius();

  return true;
}

bool ColliderBindingsProvider::bindSetSphereRadius(const char* uuid, const float radius)
{
  uuids::uuid objectUUID;
  const auto sphere = findSphere(uuid, &objectUUID);
  if (!sphere)
  {
    return false;
  }

  sphere->setRadius(radius);

  BindingContext::recordComponentEdit(objectUUID, sphere);

  return true;
}

bool ColliderBindingsProvider::bindHas(const char* uuid)
{
  return find(uuid) != nullptr;
}
