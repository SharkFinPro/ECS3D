#include "PhysicsSystem.h"
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/FiniteCheck.h>
#include <objects/components/RigidBody.h>
#include <objects/components/Transform.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <stdexcept>

namespace {
  // glm's Euler constructor composes Rz * Ry * Rx, the same order the colliders and renderer apply.
  glm::quat orientationOf(const Transform& transform)
  {
    return glm::quat(glm::radians(transform.getRotation()));
  }
}

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

  // Angular velocity is a world-space axis. Added to the Euler angles, it would turn a tilted body about
  // partly rotated axes, so a restoring torque could never right it and the body would keep spinning.
  const auto angularVelocity = body.getAngularVelocity();
  const float angularSpeed = glm::length(angularVelocity);
  if (angularSpeed > 0.0f)
  {
    const auto turn = glm::angleAxis(glm::radians(angularSpeed) * dt, angularVelocity / angularSpeed);
    transform.setRotation(glm::degrees(glm::eulerAngles(turn * orientationOf(transform))));
  }

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

  if (const auto inverseInertia = worldInverseInertia(body, transform))
  {
    body.setAngularVelocity(body.getAngularVelocity() + *inverseInertia * glm::cross(r, force));
  }
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
  if (collisionPoints.empty())
  {
    handleCollision(body, other, minimumTranslationVector, glm::vec3(0));
    return;
  }

  const auto transform = body.getOwner()->getComponent<Transform>(ComponentType::transform);
  if (!transform)
  {
    return;
  }

  const auto normal = normalize(minimumTranslationVector);
  const auto point = collisionPoints.size() < 2 ? collisionPoints[0]
                                                : supportPoint(transform->getPosition(), normal, collisionPoints);
  handleCollision(body, other, minimumTranslationVector, point);

  stopSpinIntoSupport(body, *transform, other, normal, collisionPoints);
}

void PhysicsSystem::stopSpinIntoSupport(RigidBody& body, const Transform& transform, const std::shared_ptr<Object>& other,
                                        const glm::vec3& normal, const std::span<const glm::vec3> contactPoints)
{
  const auto inverseInertia = worldInverseInertia(body, transform);
  if (!inverseInertia)
  {
    return;
  }

  const auto otherBody = other->getComponent<RigidBody>(ComponentType::rigidBody);
  const auto otherTransform = other->getComponent<Transform>(ComponentType::transform);
  const bool supportSpins = otherBody && otherTransform;
  const auto supportSpin = supportSpins ? otherBody->getAngularVelocity() : glm::vec3(0);

  // Contact responses only cancel linear velocity, so without this a box rocking from edge to edge coasts
  // through its flat pose and the next edge kicks it back, forever. Each point sheds only the spin driving
  // it into the support, weighted by the inertia, so a body tipping away from a contact keeps its spin.
  auto angularVelocity = body.getAngularVelocity();
  for (const auto& point : contactPoints.first(std::min(contactPoints.size(), maxSupportPoints)))
  {
    const auto arm = glm::cross(point - transform.getPosition(), normal);
    const float surfaceRate = supportSpins ? glm::dot(glm::cross(supportSpin, point - otherTransform->getPosition()), normal)
                                           : 0.0f;
    const float closingRate = glm::dot(angularVelocity, arm) - surfaceRate;

    const auto response = *inverseInertia * arm;
    const float resistance = glm::dot(arm, response);

    if (closingRate < 0.0f && resistance > 1e-12f)
    {
      angularVelocity -= closingRate / resistance * response;
    }
  }

  // Spin about the contact normal is left to the damping, which only approaches zero. A body resting on
  // something below it is brought the rest of the way once it is too slow to see.
  if (normal.y > 0.0f && glm::length(angularVelocity - supportSpin) < restAngularSpeed)
  {
    angularVelocity = supportSpin;
  }

  body.setAngularVelocity(angularVelocity);
}

glm::vec3 PhysicsSystem::supportPoint(const glm::vec3& centerOfMass, const glm::vec3& normal,
                                      const std::span<const glm::vec3> collisionPoints)
{
  if (collisionPoints.empty())
  {
    return centerOfMass;
  }

  const auto count = std::min(collisionPoints.size(), maxSupportPoints);

  glm::vec3 centroid{ 0 };
  for (size_t i = 0; i < count; ++i)
  {
    centroid += collisionPoints[i];
  }
  centroid /= static_cast<float>(count);

  const auto onPlane = [&centroid, &normal](const glm::vec3& point)
  {
    return point - glm::dot(point - centroid, normal) * normal;
  };

  std::array<glm::vec3, maxSupportPoints> points{};
  for (size_t i = 0; i < count; ++i)
  {
    points[i] = onPlane(collisionPoints[i]);
  }

  const auto target = onPlane(centerOfMass);

  // In the plane, the hull of at most four points is covered by the triangles among them, so the center
  // of mass is over the support region exactly when one of those triangles contains it.
  for (size_t a = 0; a < count; ++a)
  {
    for (size_t b = a + 1; b < count; ++b)
    {
      for (size_t c = b + 1; c < count; ++c)
      {
        if (triangleContains(points[a], points[b], points[c], target))
        {
          return target;
        }
      }
    }
  }

  // Outside the hull, the nearest hull point lies on a segment between two of the points.
  glm::vec3 closest = points[0];
  float closestDistance = std::numeric_limits<float>::max();
  for (size_t a = 0; a < count; ++a)
  {
    for (size_t b = a + 1; b < count; ++b)
    {
      const auto segment = points[b] - points[a];
      const auto lengthSquared = glm::dot(segment, segment);
      const auto t = lengthSquared > 1e-12f ? std::clamp(glm::dot(target - points[a], segment) / lengthSquared, 0.0f, 1.0f)
                                            : 0.0f;
      const auto candidate = points[a] + t * segment;

      const auto distance = glm::distance(candidate, target);
      if (distance < closestDistance)
      {
        closestDistance = distance;
        closest = candidate;
      }
    }
  }

  return closest;
}

bool PhysicsSystem::triangleContains(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& point)
{
  const auto ab = b - a;
  const auto ac = c - a;
  const auto ap = point - a;

  const auto abab = glm::dot(ab, ab);
  const auto abac = glm::dot(ab, ac);
  const auto acac = glm::dot(ac, ac);
  const auto apab = glm::dot(ap, ab);
  const auto apac = glm::dot(ap, ac);

  const auto denominator = abab * acac - abac * abac;
  if (denominator <= 1e-12f)
  {
    return false;
  }

  constexpr float tolerance = 1e-5f;
  const auto v = (acac * apab - abac * apac) / denominator;
  const auto w = (abab * apac - abac * apab) / denominator;

  return v >= -tolerance && w >= -tolerance && v + w <= 1.0f + tolerance;
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

std::optional<glm::mat3> PhysicsSystem::worldInverseInertia(const RigidBody& body, const Transform& transform)
{
  const auto inertiaTensor = getInertiaTensor(body, transform);

  // A degenerate body (zero mass, a zeroed scale axis) puts a zero or non-finite entry on the diagonal.
  // Inverting that gives inf/NaN angular velocity that never recovers, so the body gets no angular response
  // instead of spinning up to garbage.
  constexpr float minDiagonal = 1e-6f;
  if (inertiaTensor[0][0] <= minDiagonal || inertiaTensor[1][1] <= minDiagonal || inertiaTensor[2][2] <= minDiagonal ||
      !finiteCheck::isFinite(glm::vec3(inertiaTensor[0][0], inertiaTensor[1][1], inertiaTensor[2][2])))
  {
    return std::nullopt;
  }

  // The tensor is diagonal in the body's own axes; angular impulses are in world space.
  const auto orientation = glm::mat3_cast(orientationOf(transform));
  return orientation * glm::inverse(inertiaTensor) * glm::transpose(orientation);
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
