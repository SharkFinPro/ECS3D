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

  // TODO: applyForce is physics (it lives in ECS3DSim PhysicsSystem now, needs the Transform + inertia
  // TODO:   tensor). Forward to PhysicsSystem::applyForce(*rigidBody, *transform, { x, y, z },
  // TODO:   { px, py, pz }) once the bindings can reach the PhysicsSystem via the scripting context.
  (void)x;
  (void)y;
  (void)z;
  (void)px;
  (void)py;
  (void)pz;
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
