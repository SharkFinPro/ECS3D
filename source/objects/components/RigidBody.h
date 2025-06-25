#ifndef RIGIDBODY_H
#define RIGIDBODY_H

#include "Component.h"
#include <glm/vec3.hpp>
#include <memory>

constexpr float GRAVITY = 9.81f;

class Transform;

struct LineSegment {
  glm::vec3 start;
  glm::vec3 end;
};

class RigidBody final : public Component {
public:
  RigidBody();

  void variableUpdate(float dt) override;

  void fixedUpdate(float dt) override;

  void applyForce(const glm::vec3& force, const glm::vec3& position);

  void handleCollision(glm::vec3 minimumTranslationVector, const std::shared_ptr<Object>& other,
                       glm::vec3 collisionPoint);

  void respondToCollision(glm::vec3 minimumTranslationVector);

  [[nodiscard]] bool isFalling() const;

  void setVelocity(const glm::vec3& velocity);

  void displayGui() override;

private:
  std::vector<LineSegment> linesToDraw;

  ComponentVariable<glm::vec3> m_velocity;
  ComponentVariable<float> m_friction;
  ComponentVariable<bool> m_doGravity;
  ComponentVariable<float> m_gravity;
  ComponentVariable<glm::vec3> m_angularVelocity;

  bool falling;
  bool nextFalling;

  std::weak_ptr<Transform> transform_ptr;

  void limitMovement();
};



#endif //RIGIDBODY_H
