#ifndef RIGIDBODY_H
#define RIGIDBODY_H

#include "Component.h"
#include <glm/vec3.hpp>
#include <memory>

constexpr float GRAVITY = 9.81f;

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

  void displayGui() override;

  void start() override;

  void stop() override;

private:
  glm::vec3 initialVelocity;
  glm::vec3 liveVelocity;
  glm::vec3* currentVelocity;

  float initialFriction;
  float liveFriction;
  float* currentFriction;

  bool initialDoGravity;
  bool liveDoGravity;
  bool* currentDoGravity;

  glm::vec3 initialGravity;
  glm::vec3 liveGravity;
  glm::vec3* currentGravity;

  bool falling;
  bool nextFalling;

  std::weak_ptr<Transform> transform_ptr;

  void limitMovement();
};



#endif //RIGIDBODY_H
