#include "RigidBodyBindings.h"
#include "BindingContext.h"
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/RigidBody.h>
#include <memory>
#include <string>

namespace {
  std::shared_ptr<RigidBody> find(const char* uuid)
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

    return object->getComponent<RigidBody>(ComponentType::rigidBody);
  }
}

RigidBodyBindings RigidBodyBindingsProvider::getBindings()
{
  return RigidBodyBindings {
    .applyForce = &bindApplyForce,
    .setVelocity = &bindSetVelocity,
    .isFalling = &bindIsFalling
  };
}

void RigidBodyBindingsProvider::bindApplyForce(const char* uuid, float x, float y, float z, float px, float py, float pz)
{
  const auto rigidBody = find(uuid);
  if (!rigidBody)
  {
    return;
  }

  // Queue the force on the data instead of reaching into ECS3DSim from here; PhysicsSystem drains the
  // pending forces during its tick (keeping scripting independent of sim).
  rigidBody->addPendingForce({ x, y, z }, { px, py, pz });
}

void RigidBodyBindingsProvider::bindSetVelocity(const char* uuid, float x, float y, float z)
{
  const auto rigidBody = find(uuid);
  if (!rigidBody)
  {
    return;
  }

  rigidBody->setVelocity({ x, y, z });
}

bool RigidBodyBindingsProvider::bindIsFalling(const char* uuid)
{
  const auto rigidBody = find(uuid);
  if (!rigidBody)
  {
    return false;
  }

  return rigidBody->isFalling();
}
