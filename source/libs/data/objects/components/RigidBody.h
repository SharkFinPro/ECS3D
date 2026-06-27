#ifndef RIGIDBODY_H
#define RIGIDBODY_H

#include "Component.h"
#include <glm/vec3.hpp>
#include <vector>

class RigidBody final : public Component {
public:
  RigidBody();

  // A force queued by a script (via the applyForce binding) for the PhysicsSystem to apply next tick.
  // Buffered on the data so scripting never has to reach into ECS3DSim; the system drains it.
  struct PendingForce {
    glm::vec3 force;
    glm::vec3 position;
  };

  void addPendingForce(const glm::vec3& force, const glm::vec3& position);
  [[nodiscard]] const std::vector<PendingForce>& getPendingForces() const;
  void clearPendingForces();

  [[nodiscard]] glm::vec3 getVelocity() const;
  void setVelocity(const glm::vec3& velocity);

  [[nodiscard]] glm::vec3 getAngularVelocity() const;
  void setAngularVelocity(const glm::vec3& angularVelocity);

  [[nodiscard]] float getMass() const;
  void setMass(float mass);

  [[nodiscard]] float getFriction() const;
  void setFriction(float friction);

  [[nodiscard]] float getGravity() const;
  void setGravity(float gravity);

  [[nodiscard]] bool getDoGravity() const;
  void setDoGravity(bool doGravity);

  [[nodiscard]] bool isFalling() const;
  void setFalling(bool falling);

  [[nodiscard]] bool getNextFalling() const;
  void setNextFalling(bool nextFalling);

  [[nodiscard]] nlohmann::json serialize() override;

  void loadFromJSON(const nlohmann::json& componentData) override;

private:
  ComponentVariable<glm::vec3> m_velocity{glm::vec3(0)};
  ComponentVariable<float> m_friction{0.1f};
  ComponentVariable<bool> m_doGravity{true};
  ComponentVariable<float> m_gravity{-9.81f};
  ComponentVariable<glm::vec3> m_angularVelocity{glm::vec3(0)};
  ComponentVariable<float> m_mass{10.0f};

  bool m_falling = true;
  bool m_nextFalling = true;

  std::vector<PendingForce> m_pendingForces;
};



#endif //RIGIDBODY_H
