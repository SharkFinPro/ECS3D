#include "CameraBindings.h"
#include "BindingContext.h"
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/Camera.h>
#include <memory>
#include <string>

namespace {
  std::shared_ptr<Camera> find(const char* uuid)
  {
    const auto objectManager = BindingContext::getObjectManager();
    if (!objectManager)
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

    return object->getComponent<Camera>(ComponentType::camera);
  }
}

CameraBindings CameraBindingsProvider::getBindings()
{
  return CameraBindings {
    .getDirection = &bindGetDirection,
    .has = &bindHas
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
