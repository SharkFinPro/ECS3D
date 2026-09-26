#ifndef PHYSICSSYSTEM_H
#define PHYSICSSYSTEM_H

#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>
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

  // Public so CollisionSystem can forward collisions here and script bindings can apply forces.
  static void applyForce(RigidBody& body, const Transform& transform, const glm::vec3& force, const glm::vec3& position);

  static void handleCollision(RigidBody& body, const std::shared_ptr<Object>& other,
                              glm::vec3 minimumTranslationVector, glm::vec3 collisionPoint);

  // Resolves the pair through a single impulse at supportPoint, so a body whose center of mass sits over
  // its manifold is pushed without being torqued. A single point resolves exactly like the overload above.
  static void handleCollision(RigidBody& body, const std::shared_ptr<Object>& other,
                              glm::vec3 minimumTranslationVector, std::span<const glm::vec3> collisionPoints);

  // The center of mass projected onto the contact plane, clamped into the hull of the first
  // maxSupportPoints points. Linear and angular velocity are in different units here (per tick vs
  // degrees per second), so the impulse is placed geometrically rather than solved against spin.
  [[nodiscard]] static glm::vec3 supportPoint(const glm::vec3& centerOfMass, const glm::vec3& normal,
                                              std::span<const glm::vec3> collisionPoints);

  static constexpr size_t maxSupportPoints = 4;

  // Degrees per second. Spin a resting body still carries relative to its support below this is dropped,
  // and any spin below it is too slow to turn a body at all.
  static constexpr float restAngularSpeed = 0.01f;

private:
  [[nodiscard]] static bool triangleContains(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                                             const glm::vec3& point);

  static void integrate(RigidBody& body, Transform& transform, float dt);

  static void stopSpinIntoSupport(RigidBody& body, const Transform& transform, const std::shared_ptr<Object>& other,
                                  const glm::vec3& normal, std::span<const glm::vec3> contactPoints);

  static void respondToCollision(RigidBody& body, Transform& transform, glm::vec3 minimumTranslationVector);

  static void limitMovement(RigidBody& body, const Transform& transform);

  // Empty for a body too degenerate to invert, which then gets no angular response.
  [[nodiscard]] static std::optional<glm::mat3> worldInverseInertia(const RigidBody& body, const Transform& transform);

  [[nodiscard]] static glm::mat3x3 getInertiaTensor(const RigidBody& body, const Transform& transform);
};



#endif //PHYSICSSYSTEM_H
