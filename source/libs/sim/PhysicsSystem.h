#ifndef PHYSICSSYSTEM_H
#define PHYSICSSYSTEM_H

#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>
#include <optional>

class ObjectManager;
class Transform;
class RigidBody;

class PhysicsSystem {
public:
  // Moves every body by its velocity, then adds the tick's queued forces and gravity to that velocity. The
  // contact solver in CollisionSystem::fixedUpdate runs next and answers them before they move anything.
  static void fixedUpdate(const ObjectManager& objectManager, float dt);

  // A change of velocity at position, in units per tick like the velocity it is added to. Mass does not scale
  // it: off the center it turns the body as far as an impulse of mass times the change would. The spin is
  // stored in degrees per second, which is what needs dt.
  static void applyVelocityChange(RigidBody& body, const Transform& transform, const glm::vec3& velocityChange,
                                  const glm::vec3& position, float dt);

  // In mass times units per tick, so the same impulse moves and turns a body of twice the mass half as far.
  static void applyImpulse(RigidBody& body, const Transform& transform, const glm::vec3& impulse,
                           const glm::vec3& position, float dt);

  // A body's gravity is in meters per second squared, and the engine's content is authored at this many world units
  // per meter.
  static constexpr float unitsPerMeter = 5.0f;

  // Degrees per second. Spin a resting body still carries relative to its support below this is dropped,
  // and any spin below it is too slow to turn a body at all.
  static constexpr float restAngularSpeed = 0.01f;

  // The inertia cache is per thread and only trusted within a tick; every thread that resolves contacts clears its
  // own before its first response of the tick.
  static void clearInertiaCache();

  // Per unit mass, as a change of velocity turns the body whatever its mass, from the shapes of the body's
  // colliders (InertiaTensor.h). Empty for a body too degenerate to invert, which then gets no angular response.
  [[nodiscard]] static std::optional<glm::mat3> worldInverseInertia(const RigidBody& body, const Transform& transform);

private:
  // Moves and turns the body by its velocity and spin.
  static void integrate(RigidBody& body, Transform& transform, float dt);

  [[nodiscard]] static std::optional<glm::mat3> computeWorldInverseInertia(const RigidBody& body,
                                                                           const Transform& transform);
};

#endif //PHYSICSSYSTEM_H
