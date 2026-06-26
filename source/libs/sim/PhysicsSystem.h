#ifndef PHYSICSSYSTEM_H
#define PHYSICSSYSTEM_H

#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>
#include <memory>

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

private:
  static void integrate(RigidBody& body, Transform& transform, float dt);

  static void respondToCollision(RigidBody& body, Transform& transform, glm::vec3 minimumTranslationVector);

  static void limitMovement(RigidBody& body, const Transform& transform);

  [[nodiscard]] static glm::mat3x3 getInertiaTensor(const RigidBody& body, const Transform& transform);
};



#endif //PHYSICSSYSTEM_H
