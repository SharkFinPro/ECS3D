#ifndef PHYSICSSYSTEM_H
#define PHYSICSSYSTEM_H

#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <utility>

class ObjectManager;
class Object;
class Transform;
class RigidBody;
class Collider;

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

  // collider is the one of body's that produced the contact: on a compound body it belongs to a child, not to
  // the body's owner.
  static void handleCollision(RigidBody& body, const std::shared_ptr<Collider>& collider,
                              const std::shared_ptr<Object>& other, glm::vec3 minimumTranslationVector,
                              glm::vec3 collisionPoint, float dt);

  // Resolves the pair through a single impulse at supportPoint, so a body whose center of mass sits over
  // its manifold is pushed without being torqued. A single point resolves exactly like the overload above.
  static void handleCollision(RigidBody& body, const std::shared_ptr<Collider>& collider,
                              const std::shared_ptr<Object>& other, glm::vec3 minimumTranslationVector,
                              std::span<const glm::vec3> collisionPoints, float dt);

  // The center of mass projected onto the contact plane, clamped into the hull of the points.
  [[nodiscard]] static glm::vec3 supportPoint(const glm::vec3& centerOfMass, const glm::vec3& normal,
                                              std::span<const glm::vec3> collisionPoints);

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

  // Up to this many points, the triangles among them cover their hull.
  static constexpr size_t maxTriangulatedPoints = 4;

  [[nodiscard]] static bool isOverSupport(std::span<const glm::vec3> points, const glm::vec3& target);

  [[nodiscard]] static glm::vec3 nearestOnSupport(std::span<const glm::vec3> points, const glm::vec3& target);

  [[nodiscard]] static bool triangleContains(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                                             const glm::vec3& point);

  // One body of a contact, or none for static geometry.
  struct Side {
    RigidBody* body = nullptr;
    Transform* transform = nullptr;
    // Rested on a support last tick. In a pair of bodies the support takes the downward part of a push, so a
    // stack holds up whatever its masses, and what lands on it stops as it would on static geometry.
    bool resting = false;
    // A sphere or capsule, which can roll on its contact.
    bool rolls = false;
    // A capsule's unit axis, which it rolls about when lying along a line contact. Zero for a sphere.
    glm::vec3 rollAxis{ 0 };
    // A capsule's world radius, the lever arm of its rolling resistance. Zero for a sphere, whose arm is its
    // center's distance from the contact point.
    float rollRadius = 0.0f;
  };

  // The body resolving a contact, pushed along normal, and what it touches, pushed the other way.
  struct Pair {
    Side body;
    Side other;
    glm::vec3 normal;
    // Touching along a line under the center of mass, as a capsule lying on a flat support: friction there
    // turns the body, as it rolls, but the line still grinds against spinning about the normal.
    // A capsule within a degree of the support's plane is also laid flush into it.
    bool lineContact = false;
    // The line is the other side's: it is the capsule, and this side is the body it lies on.
    bool lineOnOther = false;
  };

  [[nodiscard]] static Pair pairOf(RigidBody& body, Transform& transform,
                                   const std::shared_ptr<Collider>& collider, const std::shared_ptr<Object>& other,
                                   const glm::vec3& normal);

  // The lever arm of the friction against turning about the normal: a face under the center of mass, or a
  // capsule's line contact under it, which also marks the pair as a line contact. Zero otherwise.
  [[nodiscard]] static float twistRadius(Pair& pair, const std::shared_ptr<Collider>& collider,
                                         const Support& support, const glm::vec3& centerOfMass,
                                         std::span<const glm::vec3> collisionPoints);

  // Marks the side as rolling when the collider is a sphere or a capsule.
  static void markRolling(Side& side, const std::shared_ptr<Collider>& collider);

  static void integrate(RigidBody& body, Transform& transform, float dt);

  // faceRadius: the lever arm of the friction against turning on a face or a capsule's line under the center of
  // mass, or zero for an edge or a point.
  static void resolve(const Pair& pair, glm::vec3 minimumTranslationVector, const glm::vec3& point, float faceRadius,
                      float dt);

  // Moves the pair apart by the translation vector, the lighter body the further.
  static void separate(const Pair& pair, glm::vec3 minimumTranslationVector);

  // Stops the velocity of the contact point along the normal, spin included. A pair of free bodies trades it
  // instead, each moving by its inverse mass. Friction follows.
  static void applyContactImpulse(const Pair& pair, const glm::vec3& point, float faceRadius, float dt);

  // Coulomb: opposes the contact point's sliding, up to the friction coefficient times load, the impulse
  // pressing the pair together this tick.
  static void applyFriction(const Pair& pair, const glm::vec3& point, float load, bool faceContact, float dt);

  // Marks a line contact whose capsule is the other side, which the body's own collider cannot tell.
  static void markLineOnOther(Pair& pair, const std::shared_ptr<Object>& other,
                              std::span<const glm::vec3> collisionPoints);

  [[nodiscard]] static glm::vec3 lineAxisOf(const Pair& pair);

  // Splits a friction direction into the part along a capsule's line contact and the rest. The line's far end
  // holds the capsule against tipping from the first, so that part acts through its center; the rest rolls it.
  // All of it is the second part unless the contact is a line and turns is set.
  [[nodiscard]] static std::pair<glm::vec3, glm::vec3> splitAlongLine(const Pair& pair, const glm::vec3& direction,
                                                                     bool turns);

  // How the side's contact point moves per unit of friction impulse along direction. The line's normal forces
  // act equally and oppositely on both sides, so the split applies to either.
  [[nodiscard]] static glm::vec3 frictionResponse(const Pair& pair, const Side& side, const glm::vec3& point,
                                                  const glm::vec3& direction, bool turns);

  // Pushes the side by a friction impulse, split by splitAlongLine.
  static void pushFriction(const Pair& pair, const Side& side, const glm::vec3& impulse, const glm::vec3& point,
                           bool turns, float dt);

  // Pushes the body against its sliding. A nonzero heldSpeed, the speed in units per tick that static friction
  // removed in stopping the slide, also takes back the slide this tick's gravity and forces caused.
  static void stopBody(const Pair& pair, const glm::vec3& point, const glm::vec3& direction, float impulse,
                       float heldSpeed, bool turns, float dt);

  // Takes back up to stopped of the body's drive step along the support, if the body was sliding that way.
  static void holdAgainstDrive(const Pair& pair, const glm::vec3& direction, float stopped);

  // A face resting on a face grinds against turning about the normal, which friction at a single support point under
  // the center cannot see.
  static void applySpinFriction(const Pair& pair, float load, float faceRadius, float dt);

  // A rolling contact does not slide, so friction does not slow it; this does.
  static void applyRollingResistance(const Pair& pair, const glm::vec3& point, float load, float dt);

  // The body's spin less the other's, in radians per tick.
  [[nodiscard]] static glm::vec3 relativeSpin(const Pair& pair, float dt);

  // Takes up to rate of relative spin about axis out of the pair, radians per tick, with at most limit of angular
  // impulse.
  static void resistSpin(const Pair& pair, const glm::vec3& axis, float rate, float limit, float dt);

  // closingSpeed is in units per tick.
  [[nodiscard]] static float restitution(const Pair& pair, float closingSpeed, float dt);

  // The normal impulse, pressed, that brings a pair where one side rests on a support to target along the
  // normal, and the part of it, squeezed, the held side takes itself rather than handing to its support.
  // pushDirection is the way the contact pushes the held side.
  [[nodiscard]] static std::pair<float, float> squeeze(const Side& held, const glm::vec3& pushDirection, float target,
                                                       float freeResistance, float heldResistance);

  // The geometric mean of the two bodies' coefficients.
  [[nodiscard]] static float frictionOf(const Pair& pair);

  // The horizontal impulse along the unit direction a resting side's own support can still hold for it this tick.
  [[nodiscard]] static float holdingCapacity(const Side& side, const glm::vec3& direction);

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

  // Lays the body flush on the face it rests on when it is about to be, or already nearly is, and settles it there.
  static void settleOnFace(RigidBody& body, Transform& transform, const std::shared_ptr<Collider>& collider,
                           const std::shared_ptr<Object>& other, const Pair& pair, const Support& support, float dt);

  static void stopSpinIntoSupport(RigidBody& body, const Transform& transform, const std::shared_ptr<Object>& other,
                                  const glm::vec3& normal, std::span<const glm::vec3> contactPoints);

  static void comeToRest(RigidBody& body, const std::shared_ptr<Object>& other, const glm::vec3& normal);

  static void respondToCollision(RigidBody& body, Transform& transform, glm::vec3 displacement);

  // Per unit mass, as a change of velocity turns the body whatever its mass. Empty for a body too degenerate to
  // invert, which then gets no angular response.
  [[nodiscard]] static std::optional<glm::mat3> worldInverseInertia(const Transform& transform);

  [[nodiscard]] static glm::mat3x3 getInertiaTensor(const Transform& transform);
};



#endif //PHYSICSSYSTEM_H
