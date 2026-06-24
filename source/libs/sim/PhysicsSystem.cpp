#include "PhysicsSystem.h"
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/RigidBody.h>
#include <objects/components/Transform.h>

void PhysicsSystem::fixedUpdate(ObjectManager& objectManager, const float dt, SimContext& context)
{
  // The dispatch that used to be a virtual call inside Object::fixedUpdate now lives here: the system
  // walks the objects and acts on the concrete RigidBody/Transform data. Only ECS3DSim (server) does.
  for (const auto& object : objectManager.getAllObjects())
  {
    const auto rigidBody = object->getComponent<RigidBody>(ComponentType::rigidBody);
    const auto transform = object->getComponent<Transform>(ComponentType::transform);

    // getComponent walks to the parent for rigidBody, so guard on ownership to integrate each body
    // exactly once (the old RigidBody::fixedUpdate ran behind an owner == this check).
    if (!rigidBody || !transform || rigidBody->getOwner() != object.get())
    {
      continue;
    }

    integrate(*rigidBody, *transform, dt);
  }

  // TODO: resolve the collisions CollisionSystem found this tick via handleCollision().
  (void)context;
}

void PhysicsSystem::integrate(RigidBody& body, Transform& transform, const float dt)
{
  // TODO: port RigidBody::fixedUpdate: apply gravity (applyForce), limitMovement, move the transform
  // TODO:   by the velocity, advance rotation by angularVelocity * dt, damp angularVelocity, and roll
  // TODO:   the falling/nextFalling state (body.setFalling(body.getNextFalling()) etc.).
  (void)body;
  (void)transform;
  (void)dt;
}

void PhysicsSystem::applyForce(RigidBody& body, Transform& transform, const glm::vec3& force, const glm::vec3& position)
{
  // TODO: port RigidBody::applyForce: add force to velocity, update falling on upward reversal, then
  // TODO:   add the angular impulse cross(position - transform.getPosition(), force) * inverse inertia.
  (void)body;
  (void)transform;
  (void)force;
  (void)position;
}

void PhysicsSystem::handleCollision(RigidBody& body, const std::shared_ptr<Object>& other,
                                    const glm::vec3 minimumTranslationVector, const glm::vec3 collisionPoint)
{
  // TODO: port RigidBody::handleCollision: respondToCollision, then resolve the impulse against the
  // TODO:   other object's RigidBody (read via Object::getComponent once Object lives in ECS3DData).
  (void)body;
  (void)other;
  (void)minimumTranslationVector;
  (void)collisionPoint;
}

void PhysicsSystem::respondToCollision(RigidBody& body, Transform& transform, const glm::vec3 minimumTranslationVector)
{
  // TODO: port RigidBody::respondToCollision: clear falling on an upward MTV, then move the transform
  // TODO:   out of penetration by the MTV.
  (void)body;
  (void)transform;
  (void)minimumTranslationVector;
}

void PhysicsSystem::limitMovement(RigidBody& body, Transform& transform)
{
  // TODO: port RigidBody::limitMovement: apply horizontal friction (-horizontalVelocity * friction)
  // TODO:   as a force at the transform's position.
  (void)body;
  (void)transform;
}

glm::mat3x3 PhysicsSystem::getInertiaTensor(const RigidBody& body, const Transform& transform)
{
  // TODO: port RigidBody::getInertiaTensor: solid-box tensor from the transform scale and body mass.
  (void)body;
  (void)transform;

  return glm::mat3x3(1.0f);
}
