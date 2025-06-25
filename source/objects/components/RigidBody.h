#ifndef RIGIDBODY_H
#define RIGIDBODY_H

// Enable to draw collision location lines
// #define COLLISION_LOCATION_DEBUG

#include "Component.h"
#include <glm/mat3x3.hpp>
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

#ifdef COLLISION_LOCATION_DEBUG
  void variableUpdate(float dt) override;
#endif

  void fixedUpdate(float dt) override;

  void applyForce(const glm::vec3& force, const glm::vec3& position);

  void handleCollision(glm::vec3 minimumTranslationVector, const std::shared_ptr<Object>& other,
                       glm::vec3 collisionPoint);

  void respondToCollision(glm::vec3 minimumTranslationVector);

  [[nodiscard]] bool isFalling() const;

  void setVelocity(const glm::vec3& velocity);

  void displayGui() override;

private:
#ifdef COLLISION_LOCATION_DEBUG
  std::vector<LineSegment> linesToDraw;
#endif

  ComponentVariable<glm::vec3> m_velocity;
  ComponentVariable<float> m_friction;
  ComponentVariable<bool> m_doGravity;
  ComponentVariable<float> m_gravity;
  ComponentVariable<glm::vec3> m_angularVelocity;
  ComponentVariable<float> m_mass;

  bool falling;
  bool nextFalling;

  std::weak_ptr<Transform> transform_ptr;

  void limitMovement();

  [[nodiscard]] glm::mat3x3 getInertiaTensor() const;
};



#endif //RIGIDBODY_H
