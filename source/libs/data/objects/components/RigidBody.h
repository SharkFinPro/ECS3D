#ifndef RIGIDBODY_H
#define RIGIDBODY_H

#include "Component.h"
#include <glm/vec3.hpp>
#include <cstdint>
#include <vector>

// How a queued force becomes a change of velocity. Velocity is in units per tick; a force or an acceleration
// acts for the tick's dt seconds, the way gravity does. The values cross the scripting ABI as ScriptBridge's
// ForceMode, so they are fixed.
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

  // What the bodies resting on this one press it down with this tick, as an impulse. Its own support carries
  // that too, so friction there holds the whole stack rather than this body alone.
  [[nodiscard]] float getStackedLoad() const;
  void setStackedLoad(float stackedLoad);

  // The friction impulse its support has already spent this tick holding it against what rests on it, which that
  // support no longer has for this body's own sliding.
  [[nodiscard]] float getSpentGrip() const;
  void setSpentGrip(float spentGrip);

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

  float m_stackedLoad = 0.0f;

  float m_spentGrip = 0.0f;

  std::vector<PendingForce> m_pendingForces;
};



#endif //RIGIDBODY_H
