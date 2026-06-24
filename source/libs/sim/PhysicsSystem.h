#ifndef PHYSICSSYSTEM_H
#define PHYSICSSYSTEM_H

#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>
#include <memory>

class SceneAsset;
class SimContext;
class Object;
class Transform;
class RigidBody;

class PhysicsSystem {
public:
  void fixedUpdate(const std::shared_ptr<SceneAsset>& scene, float dt, SimContext& context);

private:
  // The behavior lifted out of RigidBody. Each takes the data it acts on by reference instead of
  // living as a method on the component, so only ECS3DSim (server) carries the physics.
  void integrate(RigidBody& body, Transform& transform, float dt);

  void applyForce(RigidBody& body, Transform& transform, const glm::vec3& force, const glm::vec3& position);

  void handleCollision(RigidBody& body, const std::shared_ptr<Object>& other,
                       glm::vec3 minimumTranslationVector, glm::vec3 collisionPoint);

  void respondToCollision(RigidBody& body, Transform& transform, glm::vec3 minimumTranslationVector);

  void limitMovement(RigidBody& body, Transform& transform);

  [[nodiscard]] static glm::mat3x3 getInertiaTensor(const RigidBody& body, const Transform& transform);
};



#endif //PHYSICSSYSTEM_H
