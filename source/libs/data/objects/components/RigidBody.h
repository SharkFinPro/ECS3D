#ifndef RIGIDBODY_H
#define RIGIDBODY_H

#include "Component.h"
#include <glm/vec3.hpp>

class RigidBody final : public Component {
public:
  RigidBody();

  [[nodiscard]] glm::vec3 getVelocity() const;
  void setVelocity(const glm::vec3& velocity);

  [[nodiscard]] glm::vec3 getAngularVelocity() const;
  void setAngularVelocity(const glm::vec3& angularVelocity);

  [[nodiscard]] float getMass() const;
  [[nodiscard]] float getFriction() const;
  [[nodiscard]] float getGravity() const;
  [[nodiscard]] bool getDoGravity() const;

  [[nodiscard]] bool isFalling() const;
  void setFalling(bool falling);

  [[nodiscard]] bool getNextFalling() const;
  void setNextFalling(bool nextFalling);

  [[nodiscard]] nlohmann::json serialize() override;

  void loadFromJSON(const nlohmann::json& componentData) override;

private:
  // The physics (integration, applyForce, collision response, inertia tensor, friction) is lifted
  // into ECS3DSim PhysicsSystem, which reads/writes these fields through the accessors above. The
  // data class never names the system, which keeps ECS3DData independent of ECS3DSim.
  ComponentVariable<glm::vec3> m_velocity{glm::vec3(0)};
  ComponentVariable<float> m_friction{0.1f};
  ComponentVariable<bool> m_doGravity{true};
  ComponentVariable<float> m_gravity{-9.81f};
  ComponentVariable<glm::vec3> m_angularVelocity{glm::vec3(0)};
  ComponentVariable<float> m_mass{10.0f};

  bool m_falling = true;
  bool m_nextFalling = true;
};



#endif //RIGIDBODY_H
