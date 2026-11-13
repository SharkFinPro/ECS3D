#include "PhysicsSystem.h"
#include "InertiaTensor.h"
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/FiniteCheck.h>
#include <objects/components/RigidBody.h>
#include <objects/components/Transform.h>
#include <objects/components/collisions/Collider.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <optional>
#include <unordered_map>

namespace {
  struct CachedInertia {
    uint64_t worldID;
    uint64_t geometryRevision;
    bool trigger;
    std::optional<glm::mat3> inverse;
  };

  std::unordered_map<const RigidBody*, CachedInertia>& inertiaCache()
  {
    thread_local std::unordered_map<const RigidBody*, CachedInertia> cache;

    return cache;
  }

  // A push closer to the center than this gives no spin: the lever arm is too short to divide by.
  constexpr float minLeverArm = 0.01f;

  // Units per tick. A vertical speed below this is rounding left by contacts that cancel, not a body rising.
  constexpr float minRiseSpeed = 1e-5f;

  // glm's Euler constructor composes Rz * Ry * Rx, the same order the colliders and renderer apply.
  glm::quat orientationOf(const Transform& transform)
  {
    return glm::quat(glm::radians(transform.getRotation()));
  }

  glm::vec3 velocityChangeOf(const RigidBody::PendingForce& pending, const float mass, const float dt)
  {
    switch (pending.mode)
    {
      case ForceMode::force:
        return pending.force * dt / mass;
      case ForceMode::acceleration:
        return pending.force * dt;
      case ForceMode::impulse:
        return pending.force / mass;
      case ForceMode::velocityChange:
        return pending.force;
    }

    return glm::vec3(0);
  }

  // What integrate adds to the velocity each tick. Velocity is a displacement per tick, so an acceleration adds
  // a * dt * dt: that makes the fall distance after any time the same at every tick rate. The gravity is in meters
  // per second squared, so it is scaled to world units.
  glm::vec3 gravityStep(const RigidBody& body, const float dt)
  {
    return { 0.0f, body.getGravity() * PhysicsSystem::unitsPerMeter * dt * dt, 0.0f };
  }
}

void PhysicsSystem::fixedUpdate(const ObjectManager& objectManager, const float dt)
{
  clearInertiaCache();

  for (const auto& object : objectManager.getAllObjects())
  {
    const auto rigidBody = object->getComponent<RigidBody>(ComponentType::rigidBody);
    const auto transform = object->getComponent<Transform>(ComponentType::transform);

    // getComponent walks to the parent for rigidBody, so guard on ownership to integrate each body exactly once.
    if (!rigidBody || !transform || rigidBody->getOwner() != object.get())
    {
      continue;
    }

    // The body moves by the velocity the last tick's contacts left it, and only then takes this tick's forces
    // and gravity. The contact solver sees those before they move anything, so what a support or friction
    // holds never moves at all.
    integrate(*rigidBody, *transform, dt);

    // Any forces a script queued this tick (e.g. PlayerScript's input-driven movement).
    for (const auto& pending : rigidBody->getPendingForces())
    {
      applyVelocityChange(*rigidBody, *transform, velocityChangeOf(pending, rigidBody->getMass(), dt),
                          pending.position, dt);
    }
    rigidBody->clearPendingForces();

    if (rigidBody->getDoGravity())
    {
      applyVelocityChange(*rigidBody, *transform, gravityStep(*rigidBody, dt), transform->getPosition(), dt);
    }
  }
}

void PhysicsSystem::integrate(RigidBody& body, Transform& transform, const float dt)
{
  body.setFalling(body.getNextFalling());
  body.setNextFalling(true);
  body.setSupportNormal(body.getNextSupportNormal());
  body.setNextSupportNormal(glm::vec3(0));

  const auto velocity = body.getVelocity();
  if (velocity != glm::vec3(0))
  {
    transform.moveWorld(velocity);
  }

  // Angular velocity is a world-space axis. Added to the Euler angles, it would turn a tilted body about
  // partly rotated axes, so a restoring torque could never right it and the body would keep spinning.
  const auto angularVelocity = body.getAngularVelocity();
  const float angularSpeed = glm::length(angularVelocity);
  // Slower spin is kept for torque to build on, but turning by it would only rewrite a resting body's angles
  // with float noise every tick.
  if (angularSpeed >= restAngularSpeed)
  {
    const auto turn = glm::angleAxis(glm::radians(angularSpeed) * dt, angularVelocity / angularSpeed);
    transform.setWorldRotation(glm::degrees(glm::eulerAngles(turn * orientationOf(transform))));
  }

  if (angularSpeed > 0.0f)
  {
    constexpr float damping = 0.99f;
    body.setAngularVelocity(angularVelocity * damping);
  }
}

void PhysicsSystem::applyVelocityChange(RigidBody& body, const Transform& transform, const glm::vec3& velocityChange,
                                        const glm::vec3& position, const float dt)
{
  const auto velocity = body.getVelocity() + velocityChange;
  body.setVelocity(velocity);

  if (velocity.y > minRiseSpeed && velocity.y - velocityChange.y < 0)
  {
    body.setFalling(true);
    body.setNextFalling(true);
  }

  const auto r = position - transform.getPosition();
  if (glm::length(r) <= minLeverArm)
  {
    return;
  }

  if (const auto inverseInertia = worldInverseInertia(body, transform))
  {
    body.setAngularVelocity(body.getAngularVelocity() +
                            glm::degrees(*inverseInertia * glm::cross(r, velocityChange)) / dt);
  }
}

void PhysicsSystem::applyImpulse(RigidBody& body, const Transform& transform, const glm::vec3& impulse,
                                 const glm::vec3& position, const float dt)
{
  applyVelocityChange(body, transform, impulse / body.getMass(), position, dt);
}

std::optional<glm::mat3> PhysicsSystem::worldInverseInertia(const RigidBody& body, const Transform& transform)
{
  // A response asks for the same body's inverse several times between moves. A body made of its own collider
  // alone is keyed on everything its tensor reads: the world stamp (stamps are never reused, so a recycled
  // address misses) and the collider's geometry and trigger flag. A body with descendants is not cached, since
  // their transforms are not in the key.
  auto& cache = inertiaCache();

  const Object* owner = body.getOwner();
  const bool cacheable = owner && owner->getChildren().empty();
  uint64_t worldID = 0;
  uint64_t geometryRevision = 0;
  bool trigger = false;

  if (cacheable)
  {
    const auto& components = owner->getComponents();
    if (const auto it = components.find(ComponentType::collider); it != components.end())
    {
      const auto collider = static_cast<const Collider*>(it->second.get());
      geometryRevision = collider->getGeometryRevision();
      trigger = collider->isTrigger();
    }

    worldID = transform.getWorldUpdateID();

    if (const auto found = cache.find(&body); found != cache.end() && found->second.worldID == worldID &&
        found->second.geometryRevision == geometryRevision && found->second.trigger == trigger)
    {
      return found->second.inverse;
    }
  }

  const auto inverse = computeWorldInverseInertia(body, transform);

  if (cacheable)
  {
    cache[&body] = { worldID, geometryRevision, trigger, inverse };
  }

  return inverse;
}

void PhysicsSystem::clearInertiaCache()
{
  // Bounds the cache to the bodies alive this tick; entries validate themselves, so this is only housekeeping.
  inertiaCache().clear();
}

std::optional<glm::mat3> PhysicsSystem::computeWorldInverseInertia(const RigidBody& body, const Transform& transform)
{
  const auto inertiaTensor = inertia::worldTensor(body.getOwner(), transform);

  // A zeroed scale axis puts a zero or non-finite entry on the diagonal. Inverting that gives inf/NaN angular
  // velocity that never recovers, so the body gets no angular response instead of spinning up to garbage.
  constexpr float minDiagonal = 1e-6f;
  const glm::vec3 diagonal(inertiaTensor[0][0], inertiaTensor[1][1], inertiaTensor[2][2]);
  if (diagonal.x <= minDiagonal || diagonal.y <= minDiagonal || diagonal.z <= minDiagonal ||
      !finiteCheck::isFinite(diagonal))
  {
    return std::nullopt;
  }

  const auto inverse = glm::inverse(inertiaTensor);
  if (!finiteCheck::isFinite(glm::vec3(inverse[0][0], inverse[1][1], inverse[2][2])))
  {
    return std::nullopt;
  }

  return inverse;
}
