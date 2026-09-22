#include "RigidBodyBindings.h"
#include "BindingContext.h"
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/RigidBody.h>
#include <memory>
#include <string>

namespace {
  // Also hands back the parsed object uuid, so a setter that needs to record a replicated edit doesn't
  // have to re-parse the string it just resolved (see ModelRendererBindings).
  std::shared_ptr<RigidBody> find(const char* uuid, uuids::uuid* outObjectUUID = nullptr)
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

    const auto rigidBody = object->getComponent<RigidBody>(ComponentType::rigidBody);
    if (rigidBody && outObjectUUID)
    {
      *outObjectUUID = parsed.value();
    }

    return rigidBody;
  }
}

RigidBodyBindings RigidBodyBindingsProvider::getBindings()
{
  return RigidBodyBindings {
    .applyForce = &bindApplyForce,
    .setVelocity = &bindSetVelocity,
    .isFalling = &bindIsFalling,
    .has = &bindHas,
    .setAngularVelocity = &bindSetAngularVelocity,
    .getVelocity = &bindGetVelocity,
    .getAngularVelocity = &bindGetAngularVelocity,
    .getMass = &bindGetMass,
    .setMass = &bindSetMass,
    .getFriction = &bindGetFriction,
    .setFriction = &bindSetFriction,
    .getGravity = &bindGetGravity,
    .setGravity = &bindSetGravity,
    .getDoGravity = &bindGetDoGravity,
    .setDoGravity = &bindSetDoGravity
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

void RigidBodyBindingsProvider::bindSetAngularVelocity(const char* uuid, float x, float y, float z)
{
  const auto rigidBody = find(uuid);
  if (!rigidBody)
  {
    return;
  }

  rigidBody->setAngularVelocity({ x, y, z });
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

bool RigidBodyBindingsProvider::bindHas(const char* uuid)
{
  return find(uuid) != nullptr;
}

void RigidBodyBindingsProvider::bindGetVelocity(const char* uuid, float* x, float* y, float* z)
{
  const auto rigidBody = find(uuid);
  if (!rigidBody)
  {
    return;
  }

  const auto velocity = rigidBody->getVelocity();
  *x = velocity.x;
  *y = velocity.y;
  *z = velocity.z;
}

void RigidBodyBindingsProvider::bindGetAngularVelocity(const char* uuid, float* x, float* y, float* z)
{
  const auto rigidBody = find(uuid);
  if (!rigidBody)
  {
    return;
  }

  const auto angularVelocity = rigidBody->getAngularVelocity();
  *x = angularVelocity.x;
  *y = angularVelocity.y;
  *z = angularVelocity.z;
}

float RigidBodyBindingsProvider::bindGetMass(const char* uuid)
{
  const auto rigidBody = find(uuid);
  return rigidBody ? rigidBody->getMass() : 0.0f;
}

void RigidBodyBindingsProvider::bindSetMass(const char* uuid, const float mass)
{
  uuids::uuid objectUUID;
  const auto rigidBody = find(uuid, &objectUUID);
  if (!rigidBody)
  {
    return;
  }

  rigidBody->setMass(mass);

  // Not covered by the per-tick state delta (Transform only), so replicate it like the editor's own
  // component edits: buffer it here (scripting can't reach the net layer) for the app to broadcast.
  BindingContext::recordComponentEdit(objectUUID, rigidBody);
}

float RigidBodyBindingsProvider::bindGetFriction(const char* uuid)
{
  const auto rigidBody = find(uuid);
  return rigidBody ? rigidBody->getFriction() : 0.0f;
}

void RigidBodyBindingsProvider::bindSetFriction(const char* uuid, const float friction)
{
  uuids::uuid objectUUID;
  const auto rigidBody = find(uuid, &objectUUID);
  if (!rigidBody)
  {
    return;
  }

  rigidBody->setFriction(friction);

  BindingContext::recordComponentEdit(objectUUID, rigidBody);
}

float RigidBodyBindingsProvider::bindGetGravity(const char* uuid)
{
  const auto rigidBody = find(uuid);
  return rigidBody ? rigidBody->getGravity() : 0.0f;
}

void RigidBodyBindingsProvider::bindSetGravity(const char* uuid, const float gravity)
{
  uuids::uuid objectUUID;
  const auto rigidBody = find(uuid, &objectUUID);
  if (!rigidBody)
  {
    return;
  }

  rigidBody->setGravity(gravity);

  BindingContext::recordComponentEdit(objectUUID, rigidBody);
}

bool RigidBodyBindingsProvider::bindGetDoGravity(const char* uuid)
{
  const auto rigidBody = find(uuid);
  return rigidBody && rigidBody->getDoGravity();
}

void RigidBodyBindingsProvider::bindSetDoGravity(const char* uuid, const bool doGravity)
{
  uuids::uuid objectUUID;
  const auto rigidBody = find(uuid, &objectUUID);
  if (!rigidBody)
  {
    return;
  }

  rigidBody->setDoGravity(doGravity);

  BindingContext::recordComponentEdit(objectUUID, rigidBody);
}
