#ifndef RIGIDBODY_H
#define RIGIDBODY_H

// Enable to draw collision location lines
// #define COLLISION_LOCATION_DEBUG

#include "Component.h"
#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>
#include <memory>

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

  [[nodiscard]] nlohmann::json serialize() override;

private:
#ifdef COLLISION_LOCATION_DEBUG
  std::vector<LineSegment> m_linesToDraw;
#endif

  ComponentVariable<glm::vec3> m_velocity{glm::vec3(0)};
  ComponentVariable<float> m_friction{0.1f};
  ComponentVariable<bool> m_doGravity{true};
  ComponentVariable<float> m_gravity{-9.81f};
  ComponentVariable<glm::vec3> m_angularVelocity{glm::vec3(0)};
  ComponentVariable<float> m_mass{10.0f};

  bool m_falling = true;
  bool m_nextFalling = true;

  std::weak_ptr<Transform> m_transform_ptr;

  void limitMovement();

  [[nodiscard]] glm::mat3x3 getInertiaTensor() const;
};



#endif //RIGIDBODY_H
