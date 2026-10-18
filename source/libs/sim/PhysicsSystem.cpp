#include "PhysicsSystem.h"
#include "InertiaTensor.h"
#include "collisions/PlanarHull.h"
#include "collisions/SupportFaces.h"
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/FiniteCheck.h>
#include <objects/components/RigidBody.h>
#include <objects/components/Transform.h>
#include <objects/components/collisions/CapsuleCollider.h>
#include <objects/components/collisions/Collider.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace {
  // A push closer to the center than this gives no spin: the lever arm is too short to divide by.
  constexpr float minLeverArm = 0.01f;

  // About a degree. The manifold counts every corner of a face as touching only up to a small tilt, and inside
  // this a body resting on the face is laid flush with it.
  constexpr float flushTiltSine = 0.0175f;

  // Cosine of 30 degrees. A body whose face meets a neighbor closer than this is finely faceted, like a drum or a
  // ball: a tick of spin carries it across a facet, so it rolls on instead of landing on one. A box meets at 90.
  constexpr float facetedCosine = 0.866f;

  // Units per tick. Slower sliding has no direction worth normalizing.
  constexpr float minSlidingSpeed = 1e-6f;

  // Radians per tick. Slower relative spin is left to the damping.
  constexpr float minSpinRate = 1e-7f;

  // Units per second. Two bodies closing slower than this - one settling, or pressed on by gravity each tick - do
  // not bounce, or a resting pile would keep hopping.
  constexpr float minBounceSpeed = 2.0f;

  // Units per tick. A vertical speed below this is rounding left by contacts that cancel, not a body rising.
  constexpr float minRiseSpeed = 1e-5f;

  // How far a contact normal must point down for a resting body to leave the pair to the resting body above it;
  // a side-by-side contact, whose normal is level to rounding, is still resolved from both sides.
  constexpr float fromBelowNormalY = -0.5f;

  // Rolling resistance as a share of the friction coefficient: a ball on a rough floor stops within a few of its
  // own widths rather than rolling on until the angular damping wears it down.
  constexpr float rollingResistanceShare = 0.1f;

  // Each contact point sheds its share of the spin in turn, which can drive a point already visited back into
  // the support, so the points are visited again until they settle.
  constexpr int maxSpinPasses = 16;

  // A translation vector this short has no direction to normalize, so it is no contact at all.
  constexpr float minTranslationLengthSquared = 1e-12f;

  // glm's Euler constructor composes Rz * Ry * Rx, the same order the colliders and renderer apply.
  glm::quat orientationOf(const Transform& transform)
  {
    return glm::quat(glm::radians(transform.getRotation()));
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
    if (contactPoints.empty())
    {
      return 0.0f;
    }

    float total = 0.0f;
    for (const auto& point : contactPoints)
    {
      total += glm::distance(point, support);
    }

    return 2.0f / 3.0f * total / static_cast<float>(contactPoints.size());
  }

  // The mean lever arm of friction against turning about the normal for a load spread evenly along the segment
  // between two contact points: with the center of mass a and b from the ends, (a^2 + b^2) / (2 (a + b)). That is a
  // quarter of the length when centered, as a disc's even pressure gives two thirds of its radius. Zero unless the
  // center of mass is over the segment.
  float lineGrindingRadius(const glm::vec3& centerOfMass, const glm::vec3& normal,
                           const std::span<const glm::vec3> points)
  {
    const auto flat = [&normal](const glm::vec3& vector)
    {
      return vector - glm::dot(vector, normal) * normal;
    };

    const auto segment = flat(points[1] - points[0]);
    const float length = glm::length(segment);
    if (length <= 1e-6f)
    {
      return 0.0f;
    }

    const auto direction = segment / length;
    const auto offset = flat(centerOfMass - points[0]);
    const float along = glm::dot(offset, direction);
    const float beside = glm::length(offset - along * direction);

    // Float noise grows with the distance from the origin, not with the length of the segment.
    const float tolerance = 1e-4f * std::max(1.0f, glm::length(centerOfMass));
    if (along < -tolerance || along > length + tolerance || beside > tolerance)
    {
      return 0.0f;
    }

    const float toStart = std::clamp(along, 0.0f, length);
    const float toEnd = length - toStart;

    return (toStart * toStart + toEnd * toEnd) / (2.0f * length);
  }

  // What integrate adds to the velocity each tick.
  glm::vec3 gravityStep(const RigidBody& body, const float dt)
  {
    return { 0.0f, body.getGravity() * dt * 0.1f, 0.0f };
  }

  // The normal of the surface a resting body's support holds it up with: world up until a contact says otherwise.
  glm::vec3 supportNormalOf(const RigidBody& body)
  {
    const auto normal = body.getSupportNormal();

    return glm::dot(normal, normal) > 0.0f ? normal : glm::vec3(0.0f, 1.0f, 0.0f);
  }

  // The part of a vector in the plane a support's normal is perpendicular to, which its friction holds.
  glm::vec3 alongSupport(const glm::vec3& vector, const glm::vec3& normal)
  {
    return vector - glm::dot(vector, normal) * normal;
  }

  // The flattest support wins, so a body in a valley rests on the gentler side.
  void noteSupportNormal(RigidBody& body, const glm::vec3& normal)
  {
    if (normal.y > 0.0f && normal.y > body.getNextSupportNormal().y)
    {
      body.setNextSupportNormal(normal);
    }
  }

  // How much more impulse along the unit direction a support can hold, having held already this tick,
  // before friction there reaches limit. A push the other way first cancels what it has held.
  float remainingHold(const glm::vec3& held, const glm::vec3& direction, const float limit)
  {
    const float along = glm::dot(held, direction);
    const float room = along * along + limit * limit - glm::dot(held, held);

    return room < 0.0f ? 0.0f : std::max(std::sqrt(room) - along, 0.0f);
  }

  bool isSphere(const std::shared_ptr<Collider>& collider)
  {
    return collider && collider->getColliderType() == ColliderType::sphereCollider;
  }

  bool isCapsule(const std::shared_ptr<Collider>& collider)
  {
    return collider && collider->getColliderType() == ColliderType::capsuleCollider;
  }

  glm::vec3 spinOf(const std::shared_ptr<Object>& object)
  {
    const auto body = object->getComponent<RigidBody>(ComponentType::rigidBody);

    return body ? body->getAngularVelocity() : glm::vec3(0);
  }

  // The face of the support the body rests on and the face of the body turned toward it. The support's own face
  // is used, not the contact normal: across two nearly parallel faces the narrow phase may report either one, and
  // the resting body's own face would leave it exactly as tilted as it is.
  struct RestingFaces {
    collisions::SupportFace support;
    collisions::SupportFace own;
    // The largest cosine between the body's face and any other face of the body: near one for a finely faceted
    // body, zero for a box.
    float neighborCosine = 0.0f;
  };

  // A body with no collider is taken as a box turned like its owner, and a support with no collider as a plane
  // along the contact normal. Neither has a footprint to land on.
  RestingFaces ownerFace(const Transform& transform, const glm::vec3& direction)
  {
    const auto axes = glm::mat3_cast(orientationOf(transform));
    int nearest = 0;
    for (int axis = 1; axis < 3; ++axis)
    {
      if (std::fabs(glm::dot(axes[axis], direction)) > std::fabs(glm::dot(axes[nearest], direction)))
      {
        nearest = axis;
      }
    }

    const float sign = glm::dot(axes[nearest], direction) < 0.0f ? -1.0f : 1.0f;

    return { {}, { axes[nearest] * sign, glm::vec3(0), {} }, 0.0f };
  }

  // The body's face toward direction. Only a convex mesh needs all its faces, to tell how finely it is faceted.
  std::optional<RestingFaces> ownFaceToward(const Transform& transform, const std::shared_ptr<Collider>& collider,
                                            const glm::vec3& direction)
  {
    if (!collider)
    {
      return ownerFace(transform, direction);
    }

    if (collider->getColliderType() != ColliderType::convexMeshCollider)
    {
      auto face = collisions::supportFaceToward(*collider, direction);
      return face ? std::optional<RestingFaces>(RestingFaces{ {}, std::move(*face), 0.0f }) : std::nullopt;
    }

    const auto faces = collisions::supportFacesOf(*collider);
    const auto* toward = collisions::faceToward(faces, direction);
    if (!toward)
    {
      return std::nullopt;
    }

    RestingFaces resting{ {}, *toward, -1.0f };
    for (const auto& face : faces)
    {
      if (&face != toward)
      {
        resting.neighborCosine = std::max(resting.neighborCosine, glm::dot(face.normal, toward->normal));
      }
    }

    return resting;
  }

  // The face of the other object the contact normal points away from, or none when it has no face.
  std::optional<collisions::SupportFace> supportFaceOf(const std::shared_ptr<Object>& other, const glm::vec3& normal)
  {
    const auto supportCollider = other->getComponent<Collider>(ComponentType::collider);
    if (supportCollider)
    {
      return collisions::supportFaceToward(*supportCollider, normal);
    }

    return collisions::SupportFace{ normal, glm::vec3(0), {} };
  }

  std::optional<RestingFaces> restingFacesOf(const Transform& transform, const std::shared_ptr<Collider>& collider,
                                             const std::shared_ptr<Object>& other, const glm::vec3& normal)
  {
    auto support = supportFaceOf(other, normal);
    if (!support)
    {
      return std::nullopt;
    }

    auto resting = ownFaceToward(transform, collider, -support->normal);
    if (resting)
    {
      resting->support = std::move(*support);
    }

    return resting;
  }

  // The corners of the body's face, clamped onto the support's footprint the way the narrow phase clamps a manifold.
  std::vector<glm::vec3> clampedCorners(const RestingFaces& faces)
  {
    auto corners = faces.own.corners;
    for (auto& corner : corners)
    {
      corner = collisions::clampToFace(faces.support, corner);
    }

    return corners;
  }

  // Whether the spin this tick carries the face the body rests toward flat against the support, or past it.
  bool turnsFlatThisTick(const RigidBody& body, const std::shared_ptr<Object>& other, const RestingFaces& faces,
                         const float dt)
  {
    const auto bodyFace = -faces.own.normal;
    const auto axis = glm::cross(bodyFace, faces.support.normal);
    const float sine = glm::length(axis);
    if (sine <= flushTiltSine)
    {
      return true;
    }

    const float tilt = std::atan2(sine, glm::dot(bodyFace, faces.support.normal));
    const float turn = glm::dot(spinPerTick(body.getAngularVelocity() - spinOf(other), dt), axis / sine);

    return turn >= tilt;
  }

  void layFlush(Transform& transform, const RestingFaces& faces, const float maxTiltSine)
  {
    const auto orientation = orientationOf(transform);
    const auto bodyFace = -faces.own.normal;

    const float cosine = glm::dot(bodyFace, faces.support.normal);
    const auto axis = glm::cross(bodyFace, faces.support.normal);
    const float sine = glm::length(axis);

    // Anything under float noise is already flush - rewriting it would change the rotation every tick.
    constexpr float minTiltSine = 1e-5f;
    if (sine < minTiltSine || sine > maxTiltSine)
    {
      return;
    }

    const auto turn = glm::angleAxis(std::atan2(sine, cosine), axis / sine);
    transform.setWorldRotation(glm::degrees(glm::eulerAngles(turn * orientation)));
  }

  // Turns a capsule in a line contact the rest of the way into the support's plane: with its center of mass
  // between the two contact points nothing torques it down. The manifold's line band decides how tilted it may be.
  void layCapsuleFlush(Transform& transform, const glm::vec3& axis, const glm::vec3& supportNormal)
  {
    const float sine = glm::dot(axis, supportNormal);
    constexpr float minTiltSine = 1e-5f;
    if (std::fabs(sine) < minTiltSine)
    {
      return;
    }

    const auto turn = glm::angleAxis(-std::asin(sine), glm::normalize(glm::cross(axis, supportNormal)));
    transform.setWorldRotation(glm::degrees(glm::eulerAngles(turn * orientationOf(transform))));
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

    rigidBody->setDriveStep(glm::vec3(0));

    // Apply any forces a script queued this tick (e.g. PlayerScript's input-driven movement), then
    // clear them, before integrating.
    for (const auto& pending : rigidBody->getPendingForces())
    {
      const auto change = velocityChangeOf(pending, rigidBody->getMass(), dt);
      rigidBody->setDriveStep(rigidBody->getDriveStep() + change);
      applyVelocityChange(*rigidBody, *transform, change, pending.position, dt);
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
  body.setHeldImpulse(glm::vec3(0));
  body.setSupportNormal(body.getNextSupportNormal());
  body.setNextSupportNormal(glm::vec3(0));

  if (body.getDoGravity())
  {
    const auto gravity = gravityStep(body, dt);
    body.setDriveStep(body.getDriveStep() + gravity);
    applyVelocityChange(body, transform, gravity, transform.getPosition(), dt);
  }

  transform.moveWorld(body.getVelocity());

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

  constexpr float damping = 0.99f;
  body.setAngularVelocity(body.getAngularVelocity() * damping);
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

void PhysicsSystem::handleCollision(RigidBody& body, const std::shared_ptr<Collider>& collider,
                                    const std::shared_ptr<Object>& other, const glm::vec3 minimumTranslationVector,
                                    const glm::vec3 collisionPoint, const float dt)
{
  if (!other)
  {
    throw std::runtime_error("PhysicsSystem::handleCollision missing other object!");
  }

  if (glm::dot(minimumTranslationVector, minimumTranslationVector) < minTranslationLengthSquared)
  {
    return;
  }

  const auto transform = body.getOwner()->getComponent<Transform>(ComponentType::transform);
  if (!transform)
  {
    return;
  }

  resolve(pairOf(body, *transform, collider, other, normalize(minimumTranslationVector)), minimumTranslationVector,
          collisionPoint, 0.0f, dt);
}

void PhysicsSystem::handleCollision(RigidBody& body, const std::shared_ptr<Collider>& collider,
                                    const std::shared_ptr<Object>& other, const glm::vec3 minimumTranslationVector,
                                    const std::span<const glm::vec3> collisionPoints, const float dt)
{
  if (collisionPoints.empty())
  {
    handleCollision(body, collider, other, minimumTranslationVector, glm::vec3(0), dt);
    return;
  }

  if (!other)
  {
    throw std::runtime_error("PhysicsSystem::handleCollision missing other object!");
  }

  if (glm::dot(minimumTranslationVector, minimumTranslationVector) < minTranslationLengthSquared)
  {
    return;
  }

  const auto transform = body.getOwner()->getComponent<Transform>(ComponentType::transform);
  if (!transform)
  {
    return;
  }

  const auto normal = normalize(minimumTranslationVector);
  const auto support = collisionPoints.size() < 2 ? Support{ collisionPoints[0], false }
                                                  : findSupport(transform->getPosition(), normal, collisionPoints);
  auto pair = pairOf(body, *transform, collider, other, normal);
  noteSupportNormal(body, normal);

  // Two resting bodies: the one above resolves the pair in its own pass, where its support takes the push. Met again
  // from below, the overlap would be separated fully, and the pair would then touch exactly at the next tick's
  // start, which the sweep misses, letting the upper body fall unheld and slide down a slope.
  if (normal.y < fromBelowNormalY && pair.body.resting && pair.other.resting)
  {
    return;
  }

  // Before the impulse, which would otherwise answer spin into the support with a push away from it and leave
  // the body lifting off once that spin is stopped.
  stopSpinIntoSupport(body, *transform, other, normal, collisionPoints);
  const float faceRadius = twistRadius(pair, collider, support, transform->getPosition(), collisionPoints);
  markLineOnOther(pair, other, collisionPoints);
  resolve(pair, minimumTranslationVector, support.point, faceRadius, dt);

  if (normal.y > 0.0f)
  {
    settleOnFace(body, *transform, collider, other, pair, support, dt);
  }

  comeToRest(body, other, normal);
}

void PhysicsSystem::settleOnFace(RigidBody& body, Transform& transform, const std::shared_ptr<Collider>& collider,
                                  const std::shared_ptr<Object>& other, const Pair& pair, const Support& support,
                                  const float dt)
{
  if (pair.lineContact)
  {
    const auto face = supportFaceOf(other, pair.normal);
    if (face && glm::dot(pair.body.rollAxis, pair.body.rollAxis) > 0.0f)
    {
      layCapsuleFlush(transform, pair.body.rollAxis, face->normal);
    }

    return;
  }

  const auto faces = restingFacesOf(transform, collider, other, pair.normal);
  if (!faces)
  {
    return;
  }

  if (support.underCenterOfMass)
  {
    layFlush(transform, *faces, flushTiltSine);
    return;
  }

  // One tick of support at an edge turns a body a degree or more, so it would go past flat onto its other
  // edge, and back, every tick. The tick its face would reach the support, it lands on the face instead.
  if (faces->neighborCosine > facetedCosine || faces->support.corners.empty() || faces->own.corners.empty() ||
      !findSupport(transform.getPosition(), pair.normal, clampedCorners(*faces)).underCenterOfMass ||
      !turnsFlatThisTick(body, other, *faces, dt))
  {
    return;
  }

  layFlush(transform, *faces, 1.0f);

  if (const auto flat = restingFacesOf(transform, collider, other, pair.normal))
  {
    const auto flatCorners = clampedCorners(*flat);
    stopSpinIntoSupport(body, transform, other, pair.normal, flatCorners);
    const auto faceSupport = findSupport(transform.getPosition(), pair.normal, flatCorners);
    applyContactImpulse(pair, faceSupport.point, grindingRadius(faceSupport.point, flatCorners), dt);
  }
}

float PhysicsSystem::twistRadius(Pair& pair, const std::shared_ptr<Collider>& collider, const Support& support,
                                 const glm::vec3& centerOfMass, const std::span<const glm::vec3> collisionPoints)
{
  if (support.underCenterOfMass)
  {
    return grindingRadius(support.point, collisionPoints);
  }

  if (collisionPoints.size() != 2 || !isCapsule(collider))
  {
    return 0.0f;
  }

  const float radius = lineGrindingRadius(centerOfMass, pair.normal, collisionPoints);
  pair.lineContact = radius > 0.0f;

  return radius;
}

void PhysicsSystem::markLineOnOther(Pair& pair, const std::shared_ptr<Object>& other,
                                   const std::span<const glm::vec3> collisionPoints)
{
  if (pair.lineContact || collisionPoints.size() != 2 || !pair.other.body ||
      !isCapsule(other->getComponent<Collider>(ComponentType::collider)))
  {
    return;
  }

  if (lineGrindingRadius(pair.other.transform->getPosition(), pair.normal, collisionPoints) > 0.0f)
  {
    pair.lineContact = true;
    pair.lineOnOther = true;
  }
}

PhysicsSystem::Pair PhysicsSystem::pairOf(RigidBody& body, Transform& transform,
                                          const std::shared_ptr<Collider>& collider,
                                          const std::shared_ptr<Object>& other, const glm::vec3& normal)
{
  Pair pair{ { &body, &transform, false }, {}, normal };
  markRolling(pair.body, collider);

  // The other body turns about its rigid body's own center, which a child collider's owner may not be.
  const auto otherBody = other->getComponent<RigidBody>(ComponentType::rigidBody);
  const auto otherTransform = otherBody ? otherBody->getOwner()->getComponent<Transform>(ComponentType::transform)
                                        : nullptr;
  if (otherBody && otherTransform)
  {
    pair.other = { otherBody.get(), otherTransform.get(), !otherBody->isFalling() };
    markRolling(pair.other, other->getComponent<Collider>(ComponentType::collider));
    pair.body.resting = !body.isFalling();
  }

  return pair;
}

void PhysicsSystem::markRolling(Side& side, const std::shared_ptr<Collider>& collider)
{
  if (isSphere(collider))
  {
    side.rolls = true;
    return;
  }

  if (!isCapsule(collider))
  {
    return;
  }

  const auto capsule = std::static_pointer_cast<CapsuleCollider>(collider);
  const auto core = capsule->getSegment();
  const auto along = core.end - core.start;
  const float length = glm::length(along);

  side.rolls = true;
  side.rollRadius = capsule->getRadius();
  // A capsule with no core is a sphere and rolls about any axis.
  if (length > 1e-6f)
  {
    side.rollAxis = along / length;
  }
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
    const auto [pressed, squeezed] = squeeze(*held, -freeDirection, target, freeResistance, heldResistance);
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

  applyFriction(pair, point, load, faceRadius > 0.0f && !pair.lineContact, dt);
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
  auto rolling = spin - glm::dot(spin, pair.normal) * pair.normal;

  // Lying along a line, a capsule rolls only about its own axis; on a cap or a crossing contact it rolls about
  // any axis in the plane, as a sphere does.
  if (pair.lineContact)
  {
    const auto along = lineAxisOf(pair) - glm::dot(lineAxisOf(pair), pair.normal) * pair.normal;
    const float alongLength = glm::length(along);
    if (alongLength > 1e-6f)
    {
      const auto axis = along / alongLength;
      rolling = glm::dot(rolling, axis) * axis;
    }
  }

  const float rate = glm::length(rolling);
  if (rate <= minSpinRate)
  {
    return;
  }

  const float rollRadius = pair.body.rolls ? pair.body.rollRadius : pair.other.rollRadius;
  const float arm = rollRadius > 0.0f ? rollRadius : glm::distance(point, pair.body.transform->getPosition());
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
  const auto bodyInertia = worldInverseInertia(*pair.body.body, *pair.body.transform);
  if (!bodyInertia)
  {
    return;
  }

  const auto otherInertia =
    pair.other.body ? worldInverseInertia(*pair.other.body, *pair.other.transform) : std::nullopt;
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
  const float bodyResistance = glm::dot(-direction, frictionResponse(pair, pair.body, point, -direction, turns));
  const float otherResistance = glm::dot(direction, frictionResponse(pair, pair.other, point, direction, otherTurns));
  if (bodyResistance <= 0.0f)
  {
    return;
  }

  // A resting body's own support takes the drag first, as far as friction there can hold, so a light box under
  // a heavy one stays put rather than being swept along and letting the heavy one slide on.
  // In units of this impulse, which moves the resting body along its support by only the tangent part of direction.
  // A drag straight into the support goes to it whole; one straight away from it, it cannot hold at all.
  const auto supportNormal = otherHeld ? supportNormalOf(*pair.other.body) : glm::vec3(0.0f, 1.0f, 0.0f);
  const auto sideways = otherHeld ? alongSupport(direction, supportNormal) : glm::vec3(0);
  const float sidewaysShare = glm::length(sideways);
  float held = 0.0f;
  if (otherHeld && sidewaysShare > 1e-6f)
  {
    held = holdingCapacity(pair.other, sideways / sidewaysShare) / sidewaysShare;
  }
  else if (otherHeld && glm::dot(direction, supportNormal) < 0.0f)
  {
    held = std::numeric_limits<float>::max();
  }
  const float stopping = speed <= held * bodyResistance
                           ? speed / bodyResistance
                           : (speed + held * otherResistance) / (bodyResistance + otherResistance);

  // A support that has already held a push from above this tick has only the rest of its grip for this body -
  // or all of it, if that push was the other way.
  float grip = friction * load;
  if (pair.normal.y > 0.0f)
  {
    grip = std::min(grip, remainingHold(pair.body.body->getHeldImpulse(), direction, grip));
  }

  if (grip <= 0.0f)
  {
    return;
  }

  const float impulse = std::min(stopping, grip);
  stopBody(pair, point, direction, impulse, impulse >= stopping ? impulse * bodyResistance : 0.0f, turns, dt);

  // The support takes the part it can hold, and holds the resting body from turning, so only the rest moves it.
  // Turning it by the whole drag of something far heavier would spin a light body up to thousands of degrees a
  // second.
  pushFriction(pair, pair.other, std::max(impulse - held, 0.0f) * direction, point, otherTurns, dt);
  if (held > 0.0f)
  {
    pair.other.body->setHeldImpulse(pair.other.body->getHeldImpulse() + std::min(impulse, held) * sideways);
  }
}

glm::vec3 PhysicsSystem::lineAxisOf(const Pair& pair)
{
  return pair.lineOnOther ? pair.other.rollAxis : pair.body.rollAxis;
}

std::pair<glm::vec3, glm::vec3> PhysicsSystem::splitAlongLine(const Pair& pair, const glm::vec3& direction,
                                                              const bool turns)
{
  if (!turns || !pair.lineContact)
  {
    return { glm::vec3(0), direction };
  }

  const auto inPlane = lineAxisOf(pair) - glm::dot(lineAxisOf(pair), pair.normal) * pair.normal;
  const float length = glm::length(inPlane);
  if (length <= 1e-6f)
  {
    return { glm::vec3(0), direction };
  }

  const auto along = inPlane / length * glm::dot(direction, inPlane / length);

  return { along, direction - along };
}

glm::vec3 PhysicsSystem::frictionResponse(const Pair& pair, const Side& side, const glm::vec3& point,
                                          const glm::vec3& direction, const bool turns)
{
  const auto [along, across] = splitAlongLine(pair, direction, turns);

  return responseAt(side, point, along, false) + responseAt(side, point, across, turns);
}

void PhysicsSystem::pushFriction(const Pair& pair, const Side& side, const glm::vec3& impulse,
                                 const glm::vec3& point, const bool turns, const float dt)
{
  const auto [along, across] = splitAlongLine(pair, impulse, turns);
  if (along != glm::vec3(0))
  {
    push(side, along, point, false, dt);
  }
  push(side, across, point, turns, dt);
}

void PhysicsSystem::stopBody(const Pair& pair, const glm::vec3& point, const glm::vec3& direction,
                             const float impulse, const float heldSpeed, const bool turns, const float dt)
{
  pushFriction(pair, pair.body, -impulse * direction, point, turns, dt);
  if (heldSpeed > 0.0f)
  {
    holdAgainstDrive(pair, direction, heldSpeed);
  }
}

void PhysicsSystem::holdAgainstDrive(const Pair& pair, const glm::vec3& direction, const float stopped)
{
  // A rolling body turns the slide into spin instead of holding still.
  if (pair.normal.y <= 0.0f || pair.body.rolls)
  {
    return;
  }

  // Integration moved the body by this tick's drive step before the contact saw it. Separation only undoes the
  // part along the normal, so on a slope the rest is a slide that friction, holding the body, would not allow.
  const auto step = pair.body.body->getDriveStep();
  const auto drift = step - glm::dot(step, pair.normal) * pair.normal;
  const float length = glm::length(drift);
  if (length <= 0.0f || glm::dot(drift, direction) <= 0.0f)
  {
    return;
  }

  pair.body.transform->moveWorld(-drift * std::min(1.0f, stopped / length));
}

std::pair<float, float> PhysicsSystem::squeeze(const Side& held, const glm::vec3& pushDirection, const float target,
                                               const float freeResistance, const float heldResistance)
{
  const float anchored = target / freeResistance;
  const auto supportNormal = supportNormalOf(*held.body);
  const auto sideways = alongSupport(pushDirection, supportNormal);
  const auto hold = [&held, &sideways](const float impulse)
  {
    held.body->setHeldImpulse(held.body->getHeldImpulse() + impulse * sideways);
  };

  // The held side takes only the part of the push along its support, and its support's friction holds even that
  // inside the friction cone. Past it, the held side is squeezed out sideways by what its support cannot hold - but
  // only by that, rather than by the whole weight of what presses on it.
  const float slope = glm::length(sideways);
  const float cone = std::max(held.body->getFriction(), 0.0f) * std::fabs(glm::dot(pushDirection, supportNormal));
  if (slope <= cone || slope <= 1e-6f)
  {
    hold(anchored);
    return { anchored, 0.0f };
  }

  const float share = 1.0f - cone / slope;
  const float holding = holdingCapacity(held, sideways / slope) / slope;
  if (anchored * share <= holding)
  {
    hold(anchored);
    return { anchored, 0.0f };
  }

  const float pressed = (target + heldResistance * holding) / (freeResistance + heldResistance * share);
  const float squeezed = pressed * share - holding;
  hold(pressed - squeezed);

  return { pressed, squeezed };
}

float PhysicsSystem::frictionOf(const Pair& pair)
{
  // Static geometry has no coefficient of its own, so the body's applies there.
  const float own = std::max(pair.body.body->getFriction(), 0.0f);

  return pair.other.body ? std::sqrt(own * std::max(pair.other.body->getFriction(), 0.0f)) : own;
}

float PhysicsSystem::holdingCapacity(const Side& side, const glm::vec3& direction)
{
  // What the side presses its support with this tick: its own fall, which that contact is about to stop, and
  // what is stacked on it.
  const float pressing = side.body->getMass() * std::max(-side.body->getVelocity().y, 0.0f) +
                         side.body->getStackedLoad();

  return remainingHold(side.body->getHeldImpulse(), direction, std::max(side.body->getFriction(), 0.0f) * pressing);
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
  if (!side.resting || !side.body)
  {
    return direction;
  }

  const auto supportNormal = supportNormalOf(*side.body);

  return glm::dot(direction, supportNormal) < 0.0f ? alongSupport(direction, supportNormal) : direction;
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
  const auto inverseInertia = worldInverseInertia(*side.body, *side.transform);
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

void PhysicsSystem::stopSpinIntoSupport(RigidBody& body, const Transform& transform, const std::shared_ptr<Object>& other,
                                        const glm::vec3& normal, const std::span<const glm::vec3> contactPoints)
{
  const auto inverseInertia = worldInverseInertia(body, transform);
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
  for (int pass = 0; pass < maxSpinPasses; ++pass)
  {
    bool shed = false;

    for (const auto& point : contactPoints)
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

  glm::vec3 centroid{ 0 };
  for (const auto& point : collisionPoints)
  {
    centroid += point;
  }
  centroid /= static_cast<float>(collisionPoints.size());

  const auto onPlane = [&centroid, &normal](const glm::vec3& point)
  {
    return point - glm::dot(point - centroid, normal) * normal;
  };

  std::vector<glm::vec3> points;
  points.reserve(collisionPoints.size());
  for (const auto& point : collisionPoints)
  {
    points.push_back(onPlane(point));
  }

  // The hull of the points is the support region, and only its corners can bound it.
  if (points.size() > maxTriangulatedPoints)
  {
    points = collisions::convexOutline(points, normal);
  }

  const auto target = onPlane(centerOfMass);

  return isOverSupport(points, target) ? Support{ target, true } : Support{ nearestOnSupport(points, target), false };
}

bool PhysicsSystem::isOverSupport(const std::span<const glm::vec3> points, const glm::vec3& target)
{
  const size_t count = points.size();

  // In the plane, the hull of at most four points is covered by the triangles among them. A larger hull is
  // convex, so a fan from its first corner covers it.
  if (count > maxTriangulatedPoints)
  {
    for (size_t b = 1; b + 1 < count; ++b)
    {
      if (triangleContains(points[0], points[b], points[b + 1], target))
      {
        return true;
      }
    }

    return false;
  }

  for (size_t a = 0; a < count; ++a)
  {
    for (size_t b = a + 1; b < count; ++b)
    {
      for (size_t c = b + 1; c < count; ++c)
      {
        if (triangleContains(points[a], points[b], points[c], target))
        {
          return true;
        }
      }
    }
  }

  return false;
}

glm::vec3 PhysicsSystem::nearestOnSupport(const std::span<const glm::vec3> points, const glm::vec3& target)
{
  // Outside the hull, the nearest hull point lies on a segment between two of the points.
  glm::vec3 closest = points[0];
  float closestDistance = std::numeric_limits<float>::max();
  for (size_t a = 0; a < points.size(); ++a)
  {
    for (size_t b = a + 1; b < points.size(); ++b)
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

void PhysicsSystem::respondToCollision(RigidBody& body, Transform& transform, const glm::vec3 displacement)
{
  if (displacement.y > 1e-5f && body.getVelocity().y <= 1e-5f)
  {
    body.setFalling(false);
    body.setNextFalling(false);
  }

  transform.moveWorld(displacement);
}

std::optional<glm::mat3> PhysicsSystem::worldInverseInertia(const RigidBody& body, const Transform& transform)
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
