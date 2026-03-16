#ifndef RIGIDBODY_H
#define RIGIDBODY_H

// Enable to draw collision location lines
// #define COLLISION_LOCATION_DEBUG

#include "Component.h"
#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>
#include <memory>

class ECS3D;
class Transform;

struct LineSegment {
  glm::vec3 start;
  glm::vec3 end;
};

struct RigidBodyBindings
{
  void(*applyForce)(const char* uuid, float x, float y, float z, float px, float py, float pz);
  void(*setVelocity)(const char* uuid, float x, float y, float z);
};

class RigidBody final : public Component {
public:
  RigidBody();

#ifdef COLLISION_LOCATION_DEBUG
  void variableUpdate() override;
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

  void loadFromJSON(const nlohmann::json& componentData) override;

  static void initBindings(ECS3D* ecs);

  [[nodiscard]] static RigidBodyBindings getBindings();

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

  [[nodiscard]] static std::shared_ptr<RigidBody> find(const char* uuid);

  static void bindApplyForce(const char* uuid, float x, float y, float z, float px, float py, float pz);
  static void bindSetVelocity(const char* uuid, float x, float y, float z);
};



#endif //RIGIDBODY_H
