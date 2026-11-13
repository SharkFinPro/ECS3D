#ifndef RIGIDBODY_H
#define RIGIDBODY_H

#include "Component.h"
#include <glm/vec3.hpp>
#include <cstdint>
#include <vector>

// How a queued force becomes a change of velocity. Velocity is in units per tick; a force or an acceleration
// is scaled by the tick's dt seconds, in world units. A body's gravity is a separate field, in meters per second
// squared, which PhysicsSystem scales to world units by PhysicsSystem::unitsPerMeter. The values cross the
// scripting ABI as ScriptBridge's ForceMode, so they are fixed.
enum class ForceMode : std::uint8_t {
  // Mass times units per tick, per second: the velocity changes by force * dt / mass.
  force = 0,
  // Units per tick, per second: the velocity changes by acceleration * dt, whatever the mass.
  acceleration = 1,
  // Mass times units per tick: the velocity changes by impulse / mass.
  impulse = 2,
  // Units per tick, added to the velocity whatever the mass.
  velocityChange = 3
};

class RigidBody final : public Component {
public:
  RigidBody();

  // A force queued by a script (via the applyForce binding) for the PhysicsSystem to apply next tick.
  // Buffered on the data so scripting never has to reach into ECS3DSim; the system drains it.
  struct PendingForce {
    glm::vec3 force;
    glm::vec3 position;
    ForceMode mode;
  };

  void addPendingForce(const glm::vec3& force, const glm::vec3& position, ForceMode mode);
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

  // The contact normal of the support the body rested on last tick, or zero when it had none. The contact solver
  // judges by it whether a hit presses the body into what holds it up.
  [[nodiscard]] glm::vec3 getSupportNormal() const;
  void setSupportNormal(const glm::vec3& supportNormal);

  // The same normal as this tick's contacts report it, which becomes the support normal next tick.
  [[nodiscard]] glm::vec3 getNextSupportNormal() const;
  void setNextSupportNormal(const glm::vec3& nextSupportNormal);

  [[nodiscard]] nlohmann::json serialize() override;

  void loadFromJSON(const nlohmann::json& componentData) override;

  void pack(net::Message& message) const override;

  void unpack(net::MessageReader& messageReader) override;

private:
  ComponentVariable<glm::vec3> m_velocity{glm::vec3(0)};
  ComponentVariable<float> m_friction{0.5f};
  ComponentVariable<bool> m_doGravity{true};
  ComponentVariable<float> m_gravity{-9.81f};
  ComponentVariable<glm::vec3> m_angularVelocity{glm::vec3(0)};
  ComponentVariable<float> m_mass{10.0f};

  bool m_falling = true;
  bool m_nextFalling = true;

  glm::vec3 m_supportNormal{ 0 };

  glm::vec3 m_nextSupportNormal{ 0 };

  std::vector<PendingForce> m_pendingForces;
};



#endif //RIGIDBODY_H
