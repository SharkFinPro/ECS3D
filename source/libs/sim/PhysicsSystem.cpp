#include "PhysicsSystem.h"
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/FiniteCheck.h>
#include <objects/components/RigidBody.h>
#include <objects/components/Transform.h>
#include <objects/components/collisions/Collider.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>

namespace {
  // A push closer to the center than this gives no spin: the lever arm is too short to divide by.
  constexpr float minLeverArm = 0.01f;

  // About a degree. The manifold counts every corner of a face as touching only up to a small tilt, and inside
  // this a body resting on the face is laid flush with it.
  constexpr float flushTiltSine = 0.0175f;

  // Units per tick. Slower sliding has no direction worth normalizing.
  constexpr float minSlidingSpeed = 1e-6f;

  // Radians per tick. Slower relative spin is left to the damping.
  constexpr float minSpinRate = 1e-7f;

  // Units per second. Two bodies closing slower than this - one settling, or pressed on by gravity each tick - do
  // not bounce, or a resting pile would keep hopping.
  constexpr float minBounceSpeed = 2.0f;

  // Rolling resistance as a share of the friction coefficient: a ball on a rough floor stops within a few of its
  // own widths rather than rolling on until the angular damping wears it down.
  constexpr float rollingResistanceShare = 0.1f;

  // Each contact point sheds its share of the spin in turn, which can drive a point already visited back into
  // the support, so the points are visited again until they settle.
  constexpr int maxSpinPasses = 16;

  // glm's Euler constructor composes Rz * Ry * Rx, the same order the colliders and renderer apply.
  glm::quat orientationOf(const Transform& transform)
  {
    return glm::quat(glm::radians(transform.getRotation()));
  }

  int nearestAxis(const glm::mat3& axes, const glm::vec3& direction)
  {
    int nearest = 0;
    for (int i = 1; i < 3; ++i)
    {
      if (std::fabs(glm::dot(axes[i], direction)) > std::fabs(glm::dot(axes[nearest], direction)))
      {
        nearest = i;
      }
    }

    return nearest;
  }

  // The axis nearest to direction, signed to point along it: the normal of the face that way.
  glm::vec3 faceNormalToward(const glm::mat3& axes, const glm::vec3& direction)
  {
    const auto nearest = axes[nearestAxis(axes, direction)];

    return glm::dot(nearest, direction) < 0.0f ? -nearest : nearest;
  }

  // The support's own face, not the contact normal: across two nearly parallel faces the narrow phase may
  // report either one, and the resting body's own face would leave it exactly as tilted as it is.
  glm::vec3 supportFaceToward(const std::shared_ptr<Object>& support, const glm::vec3& normal)
  {
    const auto collider = support->getComponent<Collider>(ComponentType::collider);

    return collider ? faceNormalToward(glm::mat3_cast(glm::quat(glm::radians(collider->getRotation()))), normal)
                    : normal;
  }

  // Spin is stored in degrees per second, while linear velocity is a displacement per tick. Contact math
  // combines the two in radians per tick.
  glm::vec3 spinPerTick(const glm::vec3& angularVelocity, const float dt)
  {
    return glm::radians(angularVelocity) * dt;
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

  // The mean lever arm of a face's friction against turning about its normal: for an even pressure over a disc, two
  // thirds of the radius. The manifold's corners stand in for the face's edge.
  float grindingRadius(const glm::vec3& support, const std::span<const glm::vec3> contactPoints)
  {
    const auto points = contactPoints.first(std::min(contactPoints.size(), PhysicsSystem::maxSupportPoints));
    if (points.empty())
    {
      return 0.0f;
    }

    float total = 0.0f;
    for (const auto& point : points)
    {
      total += glm::distance(point, support);
    }

    return 2.0f / 3.0f * total / static_cast<float>(points.size());
  }

  bool isSphere(const std::shared_ptr<Collider>& collider)
  {
    return collider && collider->getColliderType() == ColliderType::sphereCollider;
  }

  glm::vec3 spinOf(const std::shared_ptr<Object>& object)
  {
    const auto body = object->getComponent<RigidBody>(ComponentType::rigidBody);

    return body ? body->getAngularVelocity() : glm::vec3(0);
  }

  // World-space box geometry. BoxCollider's half-extents are its scale.
  struct Box {
    glm::vec3 center;
    glm::mat3 axes;
    glm::vec3 halfExtents;
  };

  std::optional<Box> boxOf(const std::shared_ptr<Collider>& collider)
  {
    if (!collider || collider->getColliderType() != ColliderType::boxCollider)
    {
      return std::nullopt;
    }

    return Box{
      collider->getPosition(),
      glm::mat3_cast(glm::quat(glm::radians(collider->getRotation()))),
      glm::abs(collider->getScale())
    };
  }

  std::array<glm::vec3, 4> faceCorners(const Box& box, const glm::vec3& direction)
  {
    const int face = nearestAxis(box.axes, direction);
    const int u = (face + 1) % 3;
    const int v = (face + 2) % 3;

    const auto faceCenter = box.center + faceNormalToward(box.axes, direction) * box.halfExtents[face];
    const auto alongU = box.axes[u] * box.halfExtents[u];
    const auto alongV = box.axes[v] * box.halfExtents[v];

    return {
      faceCenter - alongU - alongV,
      faceCenter + alongU - alongV,
      faceCenter + alongU + alongV,
      faceCenter - alongU + alongV
    };
  }

  // Pulls a point back over the box's face toward normal, leaving its height along that face alone.
  glm::vec3 clampToFootprint(const Box& box, const glm::vec3& point, const glm::vec3& normal)
  {
    const int normalAxis = nearestAxis(box.axes, normal);

    auto clamped = box.center;
    for (int i = 0; i < 3; ++i)
    {
      const float coordinate = glm::dot(point - box.center, box.axes[i]);
      const float kept = i == normalAxis ? coordinate : std::clamp(coordinate, -box.halfExtents[i], box.halfExtents[i]);
      clamped += kept * box.axes[i];
    }

    return clamped;
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
      applyVelocityChange(*rigidBody, *transform, velocityChangeOf(pending, rigidBody->getMass(), dt),
                          pending.position, dt);
    }
    rigidBody->clearPendingForces();

    integrate(*rigidBody, *transform, dt);
  }
}

void PhysicsSystem::integrate(RigidBody& body, Transform& transform, const float dt)
{
  body.setFalling(body.getNextFalling());
  body.setNextFalling(true);
  body.setStackedLoad(0.0f);
  body.setSpentGrip(0.0f);

  if (body.getDoGravity())
  {
    const glm::vec3 gravity = { 0, body.getGravity() * dt * 0.1f, 0 };
    applyVelocityChange(body, transform, gravity, transform.getPosition(), dt);
  }

  transform.move(body.getVelocity());

  // Angular velocity is a world-space axis. Added to the Euler angles, it would turn a tilted body about
  // partly rotated axes, so a restoring torque could never right it and the body would keep spinning.
  const auto angularVelocity = body.getAngularVelocity();
  const float angularSpeed = glm::length(angularVelocity);
  // Slower spin is kept for torque to build on, but turning by it would only rewrite a resting body's angles
  // with float noise every tick.
  if (angularSpeed >= restAngularSpeed)
  {
    const auto turn = glm::angleAxis(glm::radians(angularSpeed) * dt, angularVelocity / angularSpeed);
    transform.setRotation(glm::degrees(glm::eulerAngles(turn * orientationOf(transform))));
  }

  constexpr float damping = 0.99f;
  body.setAngularVelocity(body.getAngularVelocity() * damping);
}

void PhysicsSystem::applyVelocityChange(RigidBody& body, const Transform& transform, const glm::vec3& velocityChange,
                                        const glm::vec3& position, const float dt)
{
  const auto velocity = body.getVelocity() + velocityChange;
  body.setVelocity(velocity);

  if (velocity.y > 0 && velocity.y - velocityChange.y < 0)
  {
    body.setFalling(true);
    body.setNextFalling(true);
  }

  const auto r = position - transform.getPosition();
  if (glm::length(r) <= minLeverArm)
  {
    return;
  }

  if (const auto inverseInertia = worldInverseInertia(transform))
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

void PhysicsSystem::handleCollision(RigidBody& body, const std::shared_ptr<Object>& other,
                                    const glm::vec3 minimumTranslationVector, const glm::vec3 collisionPoint,
                                    const float dt)
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

  resolve(pairOf(body, *transform, other, normalize(minimumTranslationVector)), minimumTranslationVector,
          collisionPoint, 0.0f, dt);
}

void PhysicsSystem::handleCollision(RigidBody& body, const std::shared_ptr<Object>& other,
                                    const glm::vec3 minimumTranslationVector,
                                    const std::span<const glm::vec3> collisionPoints, const float dt)
{
  if (collisionPoints.empty())
  {
    handleCollision(body, other, minimumTranslationVector, glm::vec3(0), dt);
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

  const auto normal = normalize(minimumTranslationVector);
  const auto support = collisionPoints.size() < 2 ? Support{ collisionPoints[0], false }
                                                  : findSupport(transform->getPosition(), normal, collisionPoints);
  const auto pair = pairOf(body, *transform, other, normal);

  // Before the impulse, which would otherwise answer spin into the support with a push away from it and leave
  // the body lifting off once that spin is stopped.
  stopSpinIntoSupport(body, *transform, other, normal, collisionPoints);
  const float faceRadius = support.underCenterOfMass ? grindingRadius(support.point, collisionPoints) : 0.0f;
  resolve(pair, minimumTranslationVector, support.point, faceRadius, dt);

  if (normal.y > 0.0f && support.underCenterOfMass)
  {
    layFlush(*transform, other, normal, flushTiltSine);
  }
  else if (normal.y > 0.0f)
  {
    // One tick of support at an edge turns a box a degree or more, so it would go past flat onto its other
    // edge, and back, every tick. The tick its face would reach the support, it lands on the face instead.
    const auto supportFace = supportFaceToward(other, normal);
    const auto face = restingFace(body, other, supportFace);

    if (face && findSupport(transform->getPosition(), normal, *face).underCenterOfMass &&
        turnsFlatThisTick(body, *transform, other, supportFace, dt))
    {
      layFlush(*transform, other, normal, 1.0f);

      if (const auto flatFace = restingFace(body, other, supportFace))
      {
        stopSpinIntoSupport(body, *transform, other, normal, *flatFace);
        const auto faceSupport = findSupport(transform->getPosition(), normal, *flatFace);
        applyContactImpulse(pair, faceSupport.point, grindingRadius(faceSupport.point, *flatFace), dt);
      }
    }
  }

  comeToRest(body, other, normal);
}

PhysicsSystem::Pair PhysicsSystem::pairOf(RigidBody& body, Transform& transform, const std::shared_ptr<Object>& other,
                                          const glm::vec3& normal)
{
  Pair pair{ { &body, &transform, false, isSphere(body.getOwner()->getComponent<Collider>(ComponentType::collider)) },
              {}, normal };

  // The other body turns about its rigid body's own center, which a child collider's owner may not be.
  const auto otherBody = other->getComponent<RigidBody>(ComponentType::rigidBody);
  const auto otherTransform = otherBody ? otherBody->getOwner()->getComponent<Transform>(ComponentType::transform)
                                        : nullptr;
  if (otherBody && otherTransform)
  {
    pair.other = { otherBody.get(), otherTransform.get(), !otherBody->isFalling(),
                   isSphere(other->getComponent<Collider>(ComponentType::collider)) };
    pair.body.resting = !body.isFalling();
  }

  return pair;
}

void PhysicsSystem::resolve(const Pair& pair, const glm::vec3 minimumTranslationVector, const glm::vec3& point,
                            const float faceRadius, const float dt)
{
  separate(pair, minimumTranslationVector);
  applyContactImpulse(pair, point, faceRadius, dt);
}

void PhysicsSystem::separate(const Pair& pair, const glm::vec3 minimumTranslationVector)
{
  if (!pair.other.body)
  {
    respondToCollision(*pair.body.body, *pair.body.transform, minimumTranslationVector);
    return;
  }

  const auto bodyShare = takenBy(pair.body, pair.normal) / pair.body.body->getMass();
  const auto otherShare = takenBy(pair.other, -pair.normal) / pair.other.body->getMass();
  // Positive while at most one side has its push cut down by a support, which holds because the two are pushed
  // opposite ways; if both ever were, the overlap goes to the body alone, as against static geometry.
  const float resistance = glm::dot(pair.normal, bodyShare - otherShare);
  if (resistance <= 0.0f)
  {
    respondToCollision(*pair.body.body, *pair.body.transform, minimumTranslationVector);
    return;
  }

  const float scale = glm::length(minimumTranslationVector) / resistance;

  respondToCollision(*pair.body.body, *pair.body.transform, bodyShare * scale);
  respondToCollision(*pair.other.body, *pair.other.transform, otherShare * scale);
}

void PhysicsSystem::applyContactImpulse(const Pair& pair, const glm::vec3& point, const float faceRadius,
                                        const float dt)
{
  const float closingSpeed = -glm::dot(velocityAt(pair.body, point, pair.normal, dt) -
                                       velocityAt(pair.other, point, -pair.normal, dt), pair.normal);
  if (closingSpeed <= 0.0f)
  {
    return;
  }

  const float target = (1.0f + restitution(pair, closingSpeed, dt)) * closingSpeed;

  const Side* held = pair.other.resting && pair.normal.y > 0.0f ? &pair.other
                     : pair.body.resting && pair.normal.y < 0.0f ? &pair.body
                                                                 : nullptr;
  float impulse = 0.0f;

  if (!held)
  {
    const float resistance = inverseEffectiveMass(pair, point, pair.normal);
    if (resistance <= 0.0f)
    {
      return;
    }

    impulse = target / resistance;
    push(pair.body, impulse * pair.normal, point, true, dt);
    push(pair.other, -impulse * pair.normal, point, true, dt);
  }
  else
  {
    // The free side is the one pressing down on the held one.
    const Side& freeSide = held == &pair.other ? pair.body : pair.other;
    const auto freeDirection = held == &pair.other ? pair.normal : -pair.normal;

    const float freeResistance = glm::dot(freeDirection, responseAt(freeSide, point, freeDirection, true));
    if (freeResistance <= 0.0f)
    {
      return;
    }

    // Held by its support from turning as well, the held side is pushed through its center. Measured at the point,
    // its turning would enter a response to a push it only partly takes, which can come out near zero or below
    // and have it squeezed out as if it weighed nothing.
    const float heldResistance = glm::dot(-freeDirection, responseAt(*held, point, -freeDirection, false));
    const auto [pressed, squeezed] = squeeze(*held, pair.normal, target, freeResistance, heldResistance);
    impulse = pressed;

    push(freeSide, impulse * freeDirection, point, true, dt);
    push(*held, -squeezed * freeDirection, point, false, dt);
  }

  // What rests on the body presses it into this contact too, and passes on to a resting support below.
  const float load = impulse + pair.body.body->getStackedLoad() * std::max(pair.normal.y, 0.0f);
  if (pair.other.resting && pair.normal.y > 0.0f)
  {
    pair.other.body->setStackedLoad(pair.other.body->getStackedLoad() + load * pair.normal.y);
  }

  applyFriction(pair, point, load, faceRadius > 0.0f, dt);
  applySpinFriction(pair, load, faceRadius, dt);
  applyRollingResistance(pair, point, load, dt);
}

void PhysicsSystem::applySpinFriction(const Pair& pair, const float load, const float faceRadius, const float dt)
{
  const float friction = frictionOf(pair);
  if (friction <= 0.0f || faceRadius <= 0.0f)
  {
    return;
  }

  const float rate = glm::dot(relativeSpin(pair, dt), pair.normal);
  if (std::fabs(rate) <= minSpinRate)
  {
    return;
  }

  resistSpin(pair, rate > 0.0f ? pair.normal : -pair.normal, std::fabs(rate), friction * load * faceRadius, dt);
}

void PhysicsSystem::applyRollingResistance(const Pair& pair, const glm::vec3& point, const float load, const float dt)
{
  const float resistance = rollingResistanceShare * frictionOf(pair);
  if ((!pair.body.rolls && !pair.other.rolls) || resistance <= 0.0f)
  {
    return;
  }

  const auto spin = relativeSpin(pair, dt);
  const auto rolling = spin - glm::dot(spin, pair.normal) * pair.normal;
  const float rate = glm::length(rolling);
  if (rate <= minSpinRate)
  {
    return;
  }

  const float arm = glm::distance(point, pair.body.transform->getPosition());
  resistSpin(pair, rolling / rate, rate, resistance * load * arm, dt);
}

glm::vec3 PhysicsSystem::relativeSpin(const Pair& pair, const float dt)
{
  const auto otherSpin = pair.other.body ? pair.other.body->getAngularVelocity() : glm::vec3(0);

  return spinPerTick(pair.body.body->getAngularVelocity() - otherSpin, dt);
}

void PhysicsSystem::resistSpin(const Pair& pair, const glm::vec3& axis, const float rate, const float limit,
                               const float dt)
{
  const auto bodyInertia = worldInverseInertia(*pair.body.transform);
  if (!bodyInertia)
  {
    return;
  }

  const auto otherInertia = pair.other.body ? worldInverseInertia(*pair.other.transform) : std::nullopt;
  const float bodyMass = pair.body.body->getMass();
  const float otherMass = pair.other.body ? pair.other.body->getMass() : 0.0f;

  const float resistance = glm::dot(axis, *bodyInertia * axis) / bodyMass +
                           (otherInertia ? glm::dot(axis, *otherInertia * axis) / otherMass : 0.0f);
  if (resistance <= 0.0f)
  {
    return;
  }

  const float impulse = std::min(rate / resistance, limit);

  auto& body = *pair.body.body;
  body.setAngularVelocity(body.getAngularVelocity() - glm::degrees(*bodyInertia * axis * (impulse / bodyMass)) / dt);

  if (otherInertia)
  {
    auto& other = *pair.other.body;
    other.setAngularVelocity(other.getAngularVelocity() +
                             glm::degrees(*otherInertia * axis * (impulse / otherMass)) / dt);
  }
}

void PhysicsSystem::applyFriction(const Pair& pair, const glm::vec3& point, const float load, const bool faceContact,
                                  const float dt)
{
  const float friction = frictionOf(pair);
  if (friction <= 0.0f)
  {
    return;
  }

  const auto relative = velocityAt(pair.body, point, pair.normal, dt) - velocityAt(pair.other, point, -pair.normal, dt);
  const auto sliding = relative - glm::dot(relative, pair.normal) * pair.normal;
  const float speed = glm::length(sliding);
  if (speed <= minSlidingSpeed)
  {
    return;
  }

  const auto direction = sliding / speed;

  // Resting on a face, friction does not tip the body over: its weight shifts toward the leading edge instead.
  const bool turns = !faceContact;
  const bool otherHeld = pair.other.resting && pair.normal.y > 0.0f;
  const bool otherTurns = turns && !otherHeld;
  const float bodyResistance = glm::dot(-direction, responseAt(pair.body, point, -direction, turns));
  const float otherResistance = glm::dot(direction, responseAt(pair.other, point, direction, otherTurns));
  if (bodyResistance <= 0.0f)
  {
    return;
  }

  // A resting body's own support takes the drag first, as far as friction there can hold, so a light box under
  // a heavy one stays put rather than being swept along and letting the heavy one slide on.
  const float held = otherHeld ? holdingCapacity(pair.other) : 0.0f;
  const float stopping = speed <= held * bodyResistance
                           ? speed / bodyResistance
                           : (speed + held * otherResistance) / (bodyResistance + otherResistance);

  // A support that has already held a drag from above this tick has only the rest of its grip for this body.
  const float grip = friction * load - (pair.normal.y > 0.0f ? pair.body.body->getSpentGrip() : 0.0f);
  if (grip <= 0.0f)
  {
    return;
  }

  const float impulse = std::min(stopping, grip);
  push(pair.body, -impulse * direction, point, turns, dt);

  // The support takes the part it can hold, and holds the resting body from turning, so only the rest moves it.
  // Turning it by the whole drag of something far heavier would spin a light body up to thousands of degrees a
  // second.
  push(pair.other, std::max(impulse - held, 0.0f) * direction, point, otherTurns, dt);
  if (held > 0.0f)
  {
    pair.other.body->setSpentGrip(pair.other.body->getSpentGrip() + std::min(impulse, held));
  }
}

std::pair<float, float> PhysicsSystem::squeeze(const Side& held, const glm::vec3& normal, const float target,
                                               const float freeResistance, const float heldResistance)
{
  const float anchored = target / freeResistance;

  // The held side takes only the horizontal part of the push, and its support's friction holds even that inside
  // the friction cone. Past it, the held side is squeezed out sideways by what its support cannot hold - but
  // only by that, rather than by the whole weight of what presses on it.
  const float slope = std::sqrt(normal.x * normal.x + normal.z * normal.z);
  const float cone = std::max(held.body->getFriction(), 0.0f) * std::fabs(normal.y);
  if (slope <= cone || slope <= 1e-6f)
  {
    held.body->setSpentGrip(held.body->getSpentGrip() + anchored * slope);
    return { anchored, 0.0f };
  }

  const float share = 1.0f - cone / slope;
  const float holding = holdingCapacity(held) / slope;
  if (anchored * share <= holding)
  {
    held.body->setSpentGrip(held.body->getSpentGrip() + anchored * slope);
    return { anchored, 0.0f };
  }

  const float pressed = (target + heldResistance * holding) / (freeResistance + heldResistance * share);
  const float squeezed = pressed * share - holding;
  held.body->setSpentGrip(held.body->getSpentGrip() + (pressed - squeezed) * slope);

  return { pressed, squeezed };
}

float PhysicsSystem::frictionOf(const Pair& pair)
{
  // Static geometry has no coefficient of its own, so the body's applies there.
  const float own = std::max(pair.body.body->getFriction(), 0.0f);

  return pair.other.body ? std::sqrt(own * std::max(pair.other.body->getFriction(), 0.0f)) : own;
}

float PhysicsSystem::holdingCapacity(const Side& side)
{
  // What the side presses its support with this tick: its own fall, which that contact is about to stop, and
  // what is stacked on it.
  const float pressing = side.body->getMass() * std::max(-side.body->getVelocity().y, 0.0f) +
                         side.body->getStackedLoad();

  return std::max(std::max(side.body->getFriction(), 0.0f) * pressing - side.body->getSpentGrip(), 0.0f);
}

float PhysicsSystem::restitution(const Pair& pair, const float closingSpeed, const float dt)
{
  if (!pair.other.body || closingSpeed < minBounceSpeed * dt)
  {
    return 0.0f;
  }

  // Free bodies trade their closing speed. What lands on a resting body lands on its support too, and like anything
  // landing on static geometry it does not bounce.
  const bool supported = (pair.body.resting && pair.normal.y < 0.0f) || (pair.other.resting && pair.normal.y > 0.0f);

  return supported ? 0.0f : 1.0f;
}

glm::vec3 PhysicsSystem::takenBy(const Side& side, const glm::vec3& direction)
{
  return side.resting && direction.y < 0.0f ? glm::vec3(direction.x, 0.0f, direction.z) : direction;
}

glm::vec3 PhysicsSystem::velocityAt(const Side& side, const glm::vec3& point, const glm::vec3& pushDirection,
                                     const float dt)
{
  if (!side.body)
  {
    return glm::vec3(0);
  }

  // Gravity has a resting body falling until its own support's contact stops it later in the tick, which would
  // hide how fast whatever lands on it is closing.
  auto velocity = side.body->getVelocity();
  if (side.resting && pushDirection.y < 0.0f)
  {
    velocity.y = std::max(velocity.y, 0.0f);
  }

  return velocity + glm::cross(spinPerTick(side.body->getAngularVelocity(), dt), point - side.transform->getPosition());
}

glm::vec3 PhysicsSystem::responseAt(const Side& side, const glm::vec3& point, const glm::vec3& direction,
                                     const bool turns)
{
  if (!side.body)
  {
    return glm::vec3(0);
  }

  const float inverseMass = 1.0f / side.body->getMass();
  const auto taken = takenBy(side, direction);
  auto response = taken * inverseMass;

  const auto arm = point - side.transform->getPosition();
  const auto inverseInertia = worldInverseInertia(*side.transform);
  if (turns && glm::length(arm) > minLeverArm && inverseInertia)
  {
    response += glm::cross(*inverseInertia * glm::cross(arm, taken) * inverseMass, arm);
  }

  return response;
}

float PhysicsSystem::inverseEffectiveMass(const Pair& pair, const glm::vec3& point, const glm::vec3& direction)
{
  return glm::dot(direction, responseAt(pair.body, point, direction, true) -
                             responseAt(pair.other, point, -direction, true));
}

void PhysicsSystem::push(const Side& side, const glm::vec3& impulse, const glm::vec3& point, const bool turns,
                         const float dt)
{
  if (side.body)
  {
    applyImpulse(*side.body, *side.transform, takenBy(side, impulse), turns ? point : side.transform->getPosition(),
                 dt);
  }
}

bool PhysicsSystem::turnsFlatThisTick(const RigidBody& body, const Transform& transform,
                                      const std::shared_ptr<Object>& other, const glm::vec3& supportFace,
                                      const float dt)
{
  const auto bodyFace = faceNormalToward(glm::mat3_cast(orientationOf(transform)), supportFace);
  const auto axis = glm::cross(bodyFace, supportFace);
  const float sine = glm::length(axis);
  if (sine <= flushTiltSine)
  {
    return true;
  }

  const float tilt = std::atan2(sine, glm::dot(bodyFace, supportFace));
  const float turn = glm::dot(spinPerTick(body.getAngularVelocity() - spinOf(other), dt), axis / sine);

  return turn >= tilt;
}

std::optional<std::array<glm::vec3, 4>> PhysicsSystem::restingFace(const RigidBody& body,
                                                                   const std::shared_ptr<Object>& other,
                                                                   const glm::vec3& supportFace)
{
  const auto ownBox = boxOf(body.getOwner()->getComponent<Collider>(ComponentType::collider));
  const auto supportBox = boxOf(other->getComponent<Collider>(ComponentType::collider));
  if (!ownBox || !supportBox)
  {
    return std::nullopt;
  }

  auto corners = faceCorners(*ownBox, -supportFace);
  for (auto& corner : corners)
  {
    corner = clampToFootprint(*supportBox, corner, supportFace);
  }

  return corners;
}

void PhysicsSystem::layFlush(Transform& transform, const std::shared_ptr<Object>& other, const glm::vec3& normal,
                             const float maxTiltSine)
{
  const auto supportFace = supportFaceToward(other, normal);

  const auto orientation = orientationOf(transform);
  const auto bodyFace = faceNormalToward(glm::mat3_cast(orientation), supportFace);

  const float cosine = glm::dot(bodyFace, supportFace);
  const auto axis = glm::cross(bodyFace, supportFace);
  const float sine = glm::length(axis);

  // Anything under float noise is already flush - rewriting it would change the rotation every tick.
  constexpr float minTiltSine = 1e-5f;
  if (sine < minTiltSine || sine > maxTiltSine)
  {
    return;
  }

  const auto turn = glm::angleAxis(std::atan2(sine, cosine), axis / sine);
  transform.setRotation(glm::degrees(glm::eulerAngles(turn * orientation)));
}

void PhysicsSystem::stopSpinIntoSupport(RigidBody& body, const Transform& transform, const std::shared_ptr<Object>& other,
                                        const glm::vec3& normal, const std::span<const glm::vec3> contactPoints)
{
  const auto inverseInertia = worldInverseInertia(transform);
  if (!inverseInertia)
  {
    return;
  }

  // The support turns about its rigid body's own center, which a child collider's owner may not be.
  const auto otherBody = other->getComponent<RigidBody>(ComponentType::rigidBody);
  const auto otherTransform = otherBody ? otherBody->getOwner()->getComponent<Transform>(ComponentType::transform) : nullptr;
  const bool supportSpins = otherBody && otherTransform;
  const auto supportSpin = supportSpins ? otherBody->getAngularVelocity() : glm::vec3(0);

  // The contact impulse acts at one point, so without this spin driving the other contacts into the support
  // survives it and a box rocking from edge to edge never settles. Each point sheds only the spin driving
  // it into the support, weighted by the inertia, so a body tipping away from a contact keeps its spin.
  auto angularVelocity = body.getAngularVelocity();
  const auto points = contactPoints.first(std::min(contactPoints.size(), maxSupportPoints));
  for (int pass = 0; pass < maxSpinPasses; ++pass)
  {
    bool shed = false;

    for (const auto& point : points)
    {
      const auto arm = glm::cross(point - transform.getPosition(), normal);
      const float surfaceRate = supportSpins
                                  ? glm::dot(glm::cross(supportSpin, point - otherTransform->getPosition()), normal)
                                  : 0.0f;
      const float ownRate = glm::dot(angularVelocity, arm);
      const float closingRate = ownRate - surfaceRate;

      const auto response = *inverseInertia * arm;
      const float resistance = glm::dot(arm, response);

      // Only the body's own spin toward the support is taken out. Chasing a moving surface instead would divide
      // its speed by the arm squared, which for a point under the center - any sphere's - is float noise.
      const float removedRate = std::max(closingRate, ownRate);
      if (removedRate < 0.0f && resistance > 1e-12f)
      {
        angularVelocity -= removedRate / resistance * response;
        shed = true;
      }
    }

    if (!shed)
    {
      break;
    }
  }

  body.setAngularVelocity(angularVelocity);
}

void PhysicsSystem::comeToRest(RigidBody& body, const std::shared_ptr<Object>& other, const glm::vec3& normal)
{
  // Spin about the contact normal is left to the damping, which only approaches zero. A body resting on
  // something below it is brought the rest of the way once it is too slow to see.
  const auto supportSpin = spinOf(other);
  if (normal.y > 0.0f && glm::length(body.getAngularVelocity() - supportSpin) < restAngularSpeed)
  {
    body.setAngularVelocity(supportSpin);
  }
}

glm::vec3 PhysicsSystem::supportPoint(const glm::vec3& centerOfMass, const glm::vec3& normal,
                                      const std::span<const glm::vec3> collisionPoints)
{
  return findSupport(centerOfMass, normal, collisionPoints).point;
}

PhysicsSystem::Support PhysicsSystem::findSupport(const glm::vec3& centerOfMass, const glm::vec3& normal,
                                                  const std::span<const glm::vec3> collisionPoints)
{
  if (collisionPoints.empty())
  {
    return { centerOfMass, false };
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
          return { target, true };
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

  return { closest, false };
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

void PhysicsSystem::respondToCollision(RigidBody& body, Transform& transform, const glm::vec3 displacement)
{
  if (displacement.y > 1e-5f && body.getVelocity().y <= 1e-5f)
  {
    body.setFalling(false);
    body.setNextFalling(false);
  }

  transform.move(displacement);
}

std::optional<glm::mat3> PhysicsSystem::worldInverseInertia(const Transform& transform)
{
  const auto inertiaTensor = getInertiaTensor(transform);

  // A zeroed scale axis puts a zero or non-finite entry on the diagonal. Inverting that gives inf/NaN angular
  // velocity that never recovers, so the body gets no angular response instead of spinning up to garbage.
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

glm::mat3x3 PhysicsSystem::getInertiaTensor(const Transform& transform)
{
  // A solid box per unit mass, with half-extents of its scale as BoxCollider builds it. responseAt divides by
  // the mass, and a change of velocity turns a body whatever its mass.
  const auto scale = transform.getScale();

  const auto widthSquared = scale.x * scale.x;
  const auto heightSquared = scale.y * scale.y;
  const auto depthSquared = scale.z * scale.z;

  const float Ixx = (heightSquared + depthSquared) / 3.0f;
  const float Iyy = (widthSquared + depthSquared) / 3.0f;
  const float Izz = (widthSquared + heightSquared) / 3.0f;

  return {
    Ixx, 0.0f, 0.0f,
    0.0f, Iyy, 0.0f,
    0.0f, 0.0f, Izz
  };
}
