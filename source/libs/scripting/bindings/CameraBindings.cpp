#include "CameraBindings.h"
#include "BindingContext.h"
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/Camera.h>
#include <memory>
#include <string>

namespace {
  // Also hands back the parsed object uuid, so a setter that needs to record a replicated edit doesn't
  // have to re-parse the string it just resolved (see ModelRendererBindings).
  std::shared_ptr<Camera> find(const char* uuid, uuids::uuid* outObjectUUID = nullptr)
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

    const auto camera = object->getComponent<Camera>(ComponentType::camera);
    if (camera && outObjectUUID)
    {
      *outObjectUUID = parsed.value();
    }

    return camera;
  }
}

CameraBindings CameraBindingsProvider::getBindings()
{
  return CameraBindings {
    .getDirection = &bindGetDirection,
    .has = &bindHas,
    .setDirection = &bindSetDirection,
    .getFov = &bindGetFov,
    .setFov = &bindSetFov,
    .getNearPlane = &bindGetNearPlane,
    .setNearPlane = &bindSetNearPlane,
    .getFarPlane = &bindGetFarPlane,
    .setFarPlane = &bindSetFarPlane,
    .isActive = &bindIsActive,
    .setActive = &bindSetActive
  };
}

void CameraBindingsProvider::bindGetDirection(const char* uuid, float* x, float* y, float* z)
{
  const auto camera = find(uuid);
  if (!camera)
  {
    // No camera: report a forward default so a caller that assumes it degrades safely.
    *x = 0.0f;
    *y = 0.0f;
    *z = -1.0f;
    return;
  }

  const auto direction = camera->getDirection();
  *x = direction.x;
  *y = direction.y;
  *z = direction.z;
}

bool CameraBindingsProvider::bindHas(const char* uuid)
{
  return find(uuid) != nullptr;
}

void CameraBindingsProvider::bindSetDirection(const char* uuid, float x, float y, float z)
{
  uuids::uuid objectUUID;
  const auto camera = find(uuid, &objectUUID);
  if (!camera)
  {
    return;
  }

  camera->setDirection({ x, y, z });

  // Not covered by the per-tick state delta (Transform only), so replicate it like the editor's own
  // component edits: buffer it here (scripting can't reach the net layer) for the app to broadcast.
  BindingContext::recordComponentEdit(objectUUID, camera);
}

float CameraBindingsProvider::bindGetFov(const char* uuid)
{
  const auto camera = find(uuid);
  return camera ? camera->getFov() : 45.0f;
}

void CameraBindingsProvider::bindSetFov(const char* uuid, float fov)
{
  uuids::uuid objectUUID;
  const auto camera = find(uuid, &objectUUID);
  if (!camera)
  {
    return;
  }

  camera->setFov(fov);

  BindingContext::recordComponentEdit(objectUUID, camera);
}

float CameraBindingsProvider::bindGetNearPlane(const char* uuid)
{
  const auto camera = find(uuid);
  return camera ? camera->getNearPlane() : 0.1f;
}

void CameraBindingsProvider::bindSetNearPlane(const char* uuid, float nearPlane)
{
  uuids::uuid objectUUID;
  const auto camera = find(uuid, &objectUUID);
  if (!camera)
  {
    return;
  }

  camera->setNearPlane(nearPlane);

  BindingContext::recordComponentEdit(objectUUID, camera);
}

float CameraBindingsProvider::bindGetFarPlane(const char* uuid)
{
  const auto camera = find(uuid);
  return camera ? camera->getFarPlane() : 1000.0f;
}

void CameraBindingsProvider::bindSetFarPlane(const char* uuid, float farPlane)
{
  uuids::uuid objectUUID;
  const auto camera = find(uuid, &objectUUID);
  if (!camera)
  {
    return;
  }

  camera->setFarPlane(farPlane);

  BindingContext::recordComponentEdit(objectUUID, camera);
}

bool CameraBindingsProvider::bindIsActive(const char* uuid)
{
  const auto camera = find(uuid);
  return camera ? camera->isActive() : true;
}

void CameraBindingsProvider::bindSetActive(const char* uuid, bool active)
{
  uuids::uuid objectUUID;
  const auto camera = find(uuid, &objectUUID);
  if (!camera)
  {
    return;
  }

  camera->setActive(active);

  BindingContext::recordComponentEdit(objectUUID, camera);
}
