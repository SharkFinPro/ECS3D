#include "RigidBodyBindings.h"

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
  // TODO: resolve the RigidBody (+ its Transform) for uuid, then forward to
  // TODO:   PhysicsSystem::applyForce(body, transform, { x, y, z }, { px, py, pz }).
  (void)uuid;
  (void)x;
  (void)y;
  (void)z;
  (void)px;
  (void)py;
  (void)pz;
}

void RigidBodyBindingsProvider::bindSetVelocity(const char* uuid, float x, float y, float z)
{
  // TODO: resolve the RigidBody for uuid, then body->setVelocity({ x, y, z }).
  (void)uuid;
  (void)x;
  (void)y;
  (void)z;
}

bool RigidBodyBindingsProvider::bindIsFalling(const char* uuid)
{
  // TODO: resolve the RigidBody for uuid, then return body->isFalling().
  (void)uuid;

  return false;
}
