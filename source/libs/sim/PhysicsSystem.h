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

  // A change of velocity at position, in units per tick like the velocity it is added to. Mass does not scale
  // it: off the center it turns the body as far as an impulse of mass times the change would. The spin is
  // stored in degrees per second, which is what needs dt.
  static void applyVelocityChange(RigidBody& body, const Transform& transform, const glm::vec3& velocityChange,
                                  const glm::vec3& position, float dt);

  // In mass times units per tick, so the same impulse moves and turns a body of twice the mass half as far.
  static void applyImpulse(RigidBody& body, const Transform& transform, const glm::vec3& impulse,
                           const glm::vec3& position, float dt);

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

  // One body of a contact, or none for static geometry.
  struct Side {
    RigidBody* body = nullptr;
    Transform* transform = nullptr;
    // Rested on a support last tick. In a pair of bodies the support takes the downward part of a push, so a
    // stack holds up whatever its masses, and what lands on it stops as it would on static geometry.
    bool resting = false;
  };

  // The body resolving a contact, pushed along normal, and what it touches, pushed the other way.
  struct Pair {
    Side body;
    Side other;
    glm::vec3 normal;
  };

  [[nodiscard]] static Pair pairOf(RigidBody& body, Transform& transform, const std::shared_ptr<Object>& other,
                                   const glm::vec3& normal);

  static void integrate(RigidBody& body, Transform& transform, float dt);

  // faceContact: the support is a face under the center of mass rather than an edge or a point.
  static void resolve(const Pair& pair, glm::vec3 minimumTranslationVector, const glm::vec3& point, bool faceContact,
                      float dt);

  // Moves the pair apart by the translation vector, the lighter body the further.
  static void separate(const Pair& pair, glm::vec3 minimumTranslationVector);

  // Stops the velocity of the contact point along the normal, spin included. A pair of free bodies trades it
  // instead, each moving by its inverse mass. Friction follows.
  static void applyContactImpulse(const Pair& pair, const glm::vec3& point, bool faceContact, float dt);

  // Coulomb: opposes the contact point's sliding, up to the friction coefficient times load, the impulse
  // pressing the pair together this tick.
  static void applyFriction(const Pair& pair, const glm::vec3& point, float load, bool faceContact, float dt);

  [[nodiscard]] static float restitution(const Pair& pair);

  // The geometric mean of the two bodies' coefficients.
  [[nodiscard]] static float frictionOf(const Pair& pair);

  // The friction impulse a resting side's own support can take for it this tick.
  [[nodiscard]] static float holdingCapacity(const Side& side);

  // The part of a push along direction the side takes itself.
  [[nodiscard]] static glm::vec3 takenBy(const Side& side, const glm::vec3& direction);

  // The velocity of the side's point, pushed along pushDirection, which says whether its support stops it sinking.
  [[nodiscard]] static glm::vec3 velocityAt(const Side& side, const glm::vec3& point, const glm::vec3& pushDirection,
                                            float dt);

  // How the side's point moves per unit of impulse pushing the side along direction; turns is whether it
  // acts at the point rather than through the center.
  [[nodiscard]] static glm::vec3 responseAt(const Side& side, const glm::vec3& point, const glm::vec3& direction,
                                            bool turns);

  // How fast a unit impulse along direction, on the body and back on the other, changes their relative velocity
  // at point along it.
  [[nodiscard]] static float inverseEffectiveMass(const Pair& pair, const glm::vec3& point, const glm::vec3& direction);

  static void push(const Side& side, const glm::vec3& impulse, const glm::vec3& point, bool turns, float dt);

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

  static void respondToCollision(RigidBody& body, Transform& transform, glm::vec3 displacement);

  // Per unit mass, as a change of velocity turns the body whatever its mass. Empty for a body too degenerate to
  // invert, which then gets no angular response.
  [[nodiscard]] static std::optional<glm::mat3> worldInverseInertia(const Transform& transform);

  [[nodiscard]] static glm::mat3x3 getInertiaTensor(const Transform& transform);
};



#endif //PHYSICSSYSTEM_H
