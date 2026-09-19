#include "PhysicsSystem.h"
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/FiniteCheck.h>
#include <objects/components/RigidBody.h>
#include <objects/components/Transform.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <array>
#include <stdexcept>

void PhysicsSystem::fixedUpdate(const ObjectManager& objectManager, const float dt)
{
  for (const auto& object : objectManager.getAllObjects())
  {
    const auto rigidBody = object->getComponent<RigidBody>(ComponentType::rigidBody);
    const auto transform = object->getComponent<Transform>(ComponentType::transform);

    // getComponent walks to the parent for rigidBody, so guard on ownership to integrate each body exactly once.
    if (!rigidBody || !transform || rigidBody->getOwner() != object.get())
    {
      continue;
    }

    // Apply any forces a script queued this tick (e.g. PlayerScript's input-driven movement), then
    // clear them, before integrating.
    for (const auto& pending : rigidBody->getPendingForces())
    {
      applyForce(*rigidBody, *transform, pending.force, pending.position);
    }
    rigidBody->clearPendingForces();

    integrate(*rigidBody, *transform, dt);
  }
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

void PhysicsSystem::applyForce(RigidBody& body, const Transform& transform, const glm::vec3& force, const glm::vec3& position)
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

  const auto inertiaTensor = getInertiaTensor(body, transform);

  // A degenerate body (zero mass, a zeroed scale axis) puts a zero or non-finite entry on the diagonal.
  // Inverting that gives inf/NaN angular velocity that never recovers, so skip the angular term instead
  // of spinning the body up to garbage.
  constexpr float minDiagonal = 1e-6f;
  if (inertiaTensor[0][0] <= minDiagonal || inertiaTensor[1][1] <= minDiagonal || inertiaTensor[2][2] <= minDiagonal ||
      !finiteCheck::isFinite(glm::vec3(inertiaTensor[0][0], inertiaTensor[1][1], inertiaTensor[2][2])))
  {
    return;
  }

  body.setAngularVelocity(body.getAngularVelocity() + angularImpulse * glm::inverse(inertiaTensor));
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

void PhysicsSystem::handleCollision(RigidBody& body, const std::shared_ptr<Object>& other,
                                    const glm::vec3 minimumTranslationVector, const std::span<const glm::vec3> collisionPoints)
{
  if (collisionPoints.size() < 2)
  {
    handleCollision(body, other, minimumTranslationVector, collisionPoints.empty() ? glm::vec3(0) : collisionPoints[0]);
    return;
  }

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

  const auto normal = normalize(minimumTranslationVector);
  const auto otherRb = other->getComponent<RigidBody>(ComponentType::rigidBody);

  std::shared_ptr<Transform> otherTransform;
  if (otherRb)
  {
    otherTransform = other->getComponent<Transform>(ComponentType::transform);
    if (!otherTransform)
    {
      return;
    }

    respondToCollision(*otherRb, *otherTransform, -minimumTranslationVector);
  }

  struct Side
  {
    RigidBody* body = nullptr;
    const Transform* transform = nullptr;
    glm::mat3x3 inverseInertia{ 0.0f };
  };

  // A degenerate inertia (zero mass, zeroed scale axis) contributes no spin, as in applyForce.
  const auto makeSide = [](RigidBody* rigidBody, const Transform* rigidTransform)
  {
    const auto inertia = getInertiaTensor(*rigidBody, *rigidTransform);

    constexpr float minDiagonal = 1e-6f;
    const bool spins = inertia[0][0] > minDiagonal && inertia[1][1] > minDiagonal && inertia[2][2] > minDiagonal &&
                       finiteCheck::isFinite(glm::vec3(inertia[0][0], inertia[1][1], inertia[2][2]));

    return Side{ rigidBody, rigidTransform, spins ? glm::inverse(inertia) : glm::mat3x3(0.0f) };
  };

  // Same lever-arm cutoff applyForce uses, so a point at the centre adds no spin on either side.
  const auto arm = [](const Side& side, const glm::vec3& point)
  {
    const auto r = point - side.transform->getPosition();
    return glm::length(r) <= 0.01f ? glm::vec3(0) : r;
  };

  const auto velocityAt = [&arm](const Side& side, const glm::vec3& point)
  {
    return side.body->getVelocity() + glm::cross(side.body->getAngularVelocity(), arm(side, point));
  };

  // How far one unit of impulse along the normal changes this side's velocity along it at the point.
  const auto stiffness = [&arm, &normal](const Side& side, const glm::vec3& point)
  {
    const auto torqueArm = glm::cross(arm(side, point), normal);
    return 1.0f + glm::dot(torqueArm, side.inverseInertia * torqueArm);
  };

  const auto bodySide = makeSide(&body, transform.get());
  const auto otherSide = otherRb ? makeSide(otherRb.get(), otherTransform.get()) : Side{};

  constexpr int iterations = 8;
  std::array<float, 4> accumulated{};
  const auto count = std::min(collisionPoints.size(), accumulated.size());

  for (int iteration = 0; iteration < iterations; ++iteration)
  {
    for (size_t i = 0; i < count; ++i)
    {
      const auto& point = collisionPoints[i];

      auto closing = -glm::dot(velocityAt(bodySide, point), normal);
      auto denominator = stiffness(bodySide, point);

      if (otherRb)
      {
        closing += glm::dot(velocityAt(otherSide, point), normal);
        denominator += stiffness(otherSide, point);
      }

      const auto updated = std::max(accumulated[i] + closing / denominator, 0.0f);
      const auto delta = updated - accumulated[i];
      accumulated[i] = updated;

      if (delta == 0.0f)
      {
        continue;
      }

      applyForce(body, *transform, delta * normal, point);
      if (otherRb)
      {
        applyForce(*otherRb, *otherTransform, -delta * normal, point);
      }
    }
  }
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

void PhysicsSystem::limitMovement(RigidBody& body, const Transform& transform)
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
