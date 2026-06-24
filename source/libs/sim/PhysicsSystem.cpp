#include "PhysicsSystem.h"
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/RigidBody.h>
#include <objects/components/Transform.h>
#include <glm/glm.hpp>
#include <stdexcept>

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

  (void)context;
}

void PhysicsSystem::integrate(RigidBody& body, Transform& transform, const float dt)
{
  body.setFalling(body.getNextFalling());
  body.setNextFalling(true);

  if (body.getDoGravity())
  {
    const glm::vec3 gravity = { 0, body.getGravity() * dt * 0.1f, 0 };
    applyForce(body, transform, gravity, transform.getPosition());
  }

  limitMovement(body, transform);

  transform.move(body.getVelocity());

  const auto rotation = transform.getRotation();
  const auto newRotation = rotation + body.getAngularVelocity() * dt;
  transform.setRotation(newRotation);

  constexpr float damping = 0.99f;
  body.setAngularVelocity(body.getAngularVelocity() * damping);
}

void PhysicsSystem::applyForce(RigidBody& body, Transform& transform, const glm::vec3& force, const glm::vec3& position)
{
  const auto velocity = body.getVelocity() + force;
  body.setVelocity(velocity);

  if (velocity.y > 0 && velocity.y - force.y < 0)
  {
    body.setFalling(true);
    body.setNextFalling(true);
  }

  const auto r = position - transform.getPosition();
  if (glm::length(r) <= 0.01f)
  {
    return;
  }

  const auto angularImpulse = glm::cross(r, force);

  body.setAngularVelocity(body.getAngularVelocity() + angularImpulse * glm::inverse(getInertiaTensor(body, transform)));
}

void PhysicsSystem::handleCollision(RigidBody& body, const std::shared_ptr<Object>& other,
                                    const glm::vec3 minimumTranslationVector, const glm::vec3 collisionPoint)
{
  if (!other)
  {
    throw std::runtime_error("PhysicsSystem::handleCollision missing other object!");
  }

  const auto transform = body.getOwner()->getComponent<Transform>(ComponentType::transform);
  if (!transform)
  {
    return;
  }

  respondToCollision(body, *transform, minimumTranslationVector);

  const auto collisionNormal = normalize(minimumTranslationVector);
  const auto otherRb = other->getComponent<RigidBody>(ComponentType::rigidBody);

  if (!otherRb)
  {
    const auto impulse = dot(-body.getVelocity(), collisionNormal) * collisionNormal;
    applyForce(body, *transform, impulse, collisionPoint);

    return;
  }

  const auto otherTransform = other->getComponent<Transform>(ComponentType::transform);
  if (!otherTransform)
  {
    return;
  }

  respondToCollision(*otherRb, *otherTransform, -minimumTranslationVector);

  const auto velocityDiff = otherRb->getVelocity() - body.getVelocity();

  if (dot(velocityDiff, collisionNormal) <= 0)
  {
    return;
  }

  const auto impulse = dot(velocityDiff, collisionNormal) * collisionNormal;
  applyForce(body, *transform, impulse, collisionPoint);
  applyForce(*otherRb, *otherTransform, -impulse, collisionPoint);
}

void PhysicsSystem::respondToCollision(RigidBody& body, Transform& transform, const glm::vec3 minimumTranslationVector)
{
  if (minimumTranslationVector.y > 1e-5f && body.getVelocity().y <= 1e-5f)
  {
    body.setFalling(false);
    body.setNextFalling(false);
  }

  transform.move(minimumTranslationVector);
}

void PhysicsSystem::limitMovement(RigidBody& body, Transform& transform)
{
  if (glm::length(body.getVelocity()) < 1e-5f)
  {
    return;
  }

  const glm::vec2 horizontalVelocity(body.getVelocity().x, body.getVelocity().z);
  const glm::vec2 frictionForce = -horizontalVelocity * body.getFriction();

  applyForce(body, transform, { frictionForce.x, 0.0f, frictionForce.y }, transform.getPosition());
}

glm::mat3x3 PhysicsSystem::getInertiaTensor(const RigidBody& body, const Transform& transform)
{
  const auto scale = transform.getScale();

  const auto widthSquared = scale.x * scale.x;
  const auto heightSquared = scale.y * scale.y;
  const auto depthSquared = scale.z * scale.z;

  const float factor = 1.0f / 12.0f * body.getMass() * 0.1f;

  const float Ixx = factor * (heightSquared + depthSquared);
  const float Iyy = factor * (widthSquared + depthSquared);
  const float Izz = factor * (widthSquared + heightSquared);

  return {
    Ixx, 0.0f, 0.0f,
    0.0f, Iyy, 0.0f,
    0.0f, 0.0f, Izz
  };
}
