#include "TransformBindings.h"
#include "BindingContext.h"
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/Transform.h>
#include <memory>
#include <string>

namespace {
  std::shared_ptr<Transform> find(const char* uuid)
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

    return object->getComponent<Transform>(ComponentType::transform);
  }
}

TransformBindings TransformBindingsProvider::getBindings()
{
  return TransformBindings {
    .getPosition = &bindGetPosition,
    .getScale = &bindGetScale,
    .getRotation = &bindGetRotation,
    .setScale = &bindSetScale,
    .setRotation = &bindSetRotation,
    .move = &bindMove,
    .start = &bindStart,
    .stop = &bindStop,
    .has = &bindHas
  };
}

void TransformBindingsProvider::bindGetPosition(const char* uuid, float* x, float* y, float* z)
{
  const auto transform = find(uuid);
  if (!transform)
  {
    return;
  }

  const auto position = transform->getPosition();
  *x = position.x;
  *y = position.y;
  *z = position.z;
}

void TransformBindingsProvider::bindGetScale(const char* uuid, float* x, float* y, float* z)
{
  const auto transform = find(uuid);
  if (!transform)
  {
    return;
  }

  const auto scale = transform->getScale();
  *x = scale.x;
  *y = scale.y;
  *z = scale.z;
}

void TransformBindingsProvider::bindGetRotation(const char* uuid, float* x, float* y, float* z)
{
  const auto transform = find(uuid);
  if (!transform)
  {
    return;
  }

  const auto rotation = transform->getRotation();
  *x = rotation.x;
  *y = rotation.y;
  *z = rotation.z;
}

void TransformBindingsProvider::bindSetScale(const char* uuid, float x, float y, float z)
{
  const auto transform = find(uuid);
  if (!transform)
  {
    return;
  }

  transform->setScale({ x, y, z });
}

void TransformBindingsProvider::bindSetRotation(const char* uuid, float x, float y, float z)
{
  const auto transform = find(uuid);
  if (!transform)
  {
    return;
  }

  transform->setRotation({ x, y, z });
}

void TransformBindingsProvider::bindMove(const char* uuid, float x, float y, float z)
{
  const auto transform = find(uuid);
  if (!transform)
  {
    return;
  }

  transform->move({ x, y, z });
}

void TransformBindingsProvider::bindStart(const char* uuid)
{
  const auto transform = find(uuid);
  if (!transform)
  {
    return;
  }

  transform->start();
}

void TransformBindingsProvider::bindStop(const char* uuid)
{
  const auto transform = find(uuid);
  if (!transform)
  {
    return;
  }

  transform->stop();
}

bool TransformBindingsProvider::bindHas(const char* uuid)
{
  return find(uuid) != nullptr;
}
