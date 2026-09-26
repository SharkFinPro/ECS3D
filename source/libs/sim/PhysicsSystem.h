#ifndef PHYSICSSYSTEM_H
#define PHYSICSSYSTEM_H

#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>
#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>

class ObjectManager;
class Object;
class Transform;
class RigidBody;

class PhysicsSystem {
public:
  static void fixedUpdate(const ObjectManager& objectManager, float dt);

  // Public so CollisionSystem can forward collisions here and script bindings can apply forces. force is a
  // change of velocity in units per tick, like the velocity it is added to; the spin it adds is stored in
  // degrees per second, which is what needs dt.
  static void applyForce(RigidBody& body, const Transform& transform, const glm::vec3& force, const glm::vec3& position,
                         float dt);

  static void handleCollision(RigidBody& body, const std::shared_ptr<Object>& other,
                              glm::vec3 minimumTranslationVector, glm::vec3 collisionPoint, float dt);

  // Resolves the pair through a single impulse at supportPoint, so a body whose center of mass sits over
  // its manifold is pushed without being torqued. A single point resolves exactly like the overload above.
  static void handleCollision(RigidBody& body, const std::shared_ptr<Object>& other,
                              glm::vec3 minimumTranslationVector, std::span<const glm::vec3> collisionPoints, float dt);

  // The center of mass projected onto the contact plane, clamped into the hull of the first
  // maxSupportPoints points.
  [[nodiscard]] static glm::vec3 supportPoint(const glm::vec3& centerOfMass, const glm::vec3& normal,
                                              std::span<const glm::vec3> collisionPoints);

  static constexpr size_t maxSupportPoints = 4;

  // Degrees per second. Spin a resting body still carries relative to its support below this is dropped,
  // and any spin below it is too slow to turn a body at all.
  static constexpr float restAngularSpeed = 0.01f;

private:
  struct Support {
    glm::vec3 point;
    bool underCenterOfMass;
  };

  // supportPoint, and whether the center of mass is over the manifold rather than clamped onto its edge.
  [[nodiscard]] static Support findSupport(const glm::vec3& centerOfMass, const glm::vec3& normal,
                                           std::span<const glm::vec3> collisionPoints);

  [[nodiscard]] static bool triangleContains(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                                             const glm::vec3& point);

  static void integrate(RigidBody& body, Transform& transform, float dt);

  // Stops the velocity of the contact point along normal, or for a dynamic pair exchanges it, spin included.
  static void applyContactImpulse(RigidBody& body, const Transform& transform, const std::shared_ptr<Object>& other,
                                  const glm::vec3& normal, const glm::vec3& point, float dt);

  // How much less a push at point moves the contact than it would a body that cannot turn: 1 plus the
  // share the spin takes.
  [[nodiscard]] static float inverseEffectiveMass(const Transform& transform, const glm::vec3& point,
                                                  const glm::vec3& normal);

  static void stopSpinIntoSupport(RigidBody& body, const Transform& transform, const std::shared_ptr<Object>& other,
                                  const glm::vec3& normal, std::span<const glm::vec3> contactPoints);

  static void comeToRest(RigidBody& body, const std::shared_ptr<Object>& other, const glm::vec3& normal);

  // Whether the spin this tick carries the face the body rests toward flat against the support, or past it.
  [[nodiscard]] static bool turnsFlatThisTick(const RigidBody& body, const Transform& transform,
                                              const std::shared_ptr<Object>& other, const glm::vec3& supportFace,
                                              float dt);

  // The corners of the body's box face turned toward the support box, clamped onto its footprint the way the
  // narrow phase clamps a manifold. Empty unless both colliders are boxes.
  [[nodiscard]] static std::optional<std::array<glm::vec3, 4>> restingFace(const RigidBody& body,
                                                                           const std::shared_ptr<Object>& other,
                                                                           const glm::vec3& supportFace);

  static void layFlush(Transform& transform, const std::shared_ptr<Object>& other, const glm::vec3& normal,
                       float maxTiltSine);

  static void respondToCollision(RigidBody& body, Transform& transform, glm::vec3 minimumTranslationVector);

  static void limitMovement(RigidBody& body, const Transform& transform, float dt);

  // Per unit mass: a force here is already a change of velocity. Empty for a body too degenerate to invert,
  // which then gets no angular response.
  [[nodiscard]] static std::optional<glm::mat3> worldInverseInertia(const Transform& transform);

  [[nodiscard]] static glm::mat3x3 getInertiaTensor(const Transform& transform);
};



#endif //PHYSICSSYSTEM_H
