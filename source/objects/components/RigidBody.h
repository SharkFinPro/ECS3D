#ifndef RIGIDBODY_H
#define RIGIDBODY_H

#include "Component.h"
#include <glm/vec3.hpp>
#include <memory>

class Transform;

class RigidBody final : public Component {
public:
  RigidBody();

  void fixedUpdate(float dt) override;

  void applyForce(const glm::vec3& force);

  void handleCollision(glm::vec3 minimumTranslationVector, const std::shared_ptr<Object>& other);

  void respondToCollision(glm::vec3 minimumTranslationVector);

  [[nodiscard]] bool isFalling() const;

  void setVelocity(const glm::vec3& velocity);

private:
  glm::vec3 velocity;

  bool doGravity;
  glm::vec3 gravity;

  bool falling;
  bool nextFalling;

  std::weak_ptr<Transform> transform_ptr;

  void limitMovement();
};



#endif //RIGIDBODY_H
