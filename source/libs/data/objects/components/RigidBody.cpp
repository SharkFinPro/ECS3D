#include "RigidBody.h"
#include "FiniteCheck.h"
#include "WireTypes.h"
#include <nlohmann/json.hpp>
#include <Protocol.h>
#include <algorithm>

namespace {
  // Zero or negative mass is no body at all - floored here so every path that can set mass (the setter,
  // loading a project, unpacking a replicated snapshot) shares one rule instead of each guarding it separately.
  constexpr float kMinMass = 0.001f;
}

RigidBody::RigidBody()
  : Component(ComponentType::rigidBody)
{
  loadVariable(m_velocity);
  loadVariable(m_friction);
  loadVariable(m_doGravity);
  loadVariable(m_gravity);
  loadVariable(m_angularVelocity);
  loadVariable(m_mass);
}

void RigidBody::addPendingForce(const glm::vec3& force, const glm::vec3& position, const ForceMode mode)
{
  m_pendingForces.push_back({ force, position, mode });
}

const std::vector<RigidBody::PendingForce>& RigidBody::getPendingForces() const
{
  return m_pendingForces;
}

void RigidBody::clearPendingForces()
{
  m_pendingForces.clear();
}

glm::vec3 RigidBody::getVelocity() const
{
  return m_velocity.get();
}

void RigidBody::setVelocity(const glm::vec3& velocity)
{
  if (!finiteCheck::isFinite(velocity))
  {
    return;
  }

  m_velocity.set(velocity);
}

glm::vec3 RigidBody::getAngularVelocity() const
{
  return m_angularVelocity.get();
}

void RigidBody::setAngularVelocity(const glm::vec3& angularVelocity)
{
  if (!finiteCheck::isFinite(angularVelocity))
  {
    return;
  }

  m_angularVelocity.set(angularVelocity);
}

float RigidBody::getMass() const
{
  return m_mass.get();
}

void RigidBody::setMass(const float mass)
{
  if (!finiteCheck::isFinite(mass))
  {
    return;
  }

  m_mass.set(std::max(mass, kMinMass));
}

float RigidBody::getFriction() const
{
  return m_friction.get();
}

void RigidBody::setFriction(const float friction)
{
  if (!finiteCheck::isFinite(friction))
  {
    return;
  }

  m_friction.set(friction);
}

float RigidBody::getGravity() const
{
  return m_gravity.get();
}

void RigidBody::setGravity(const float gravity)
{
  if (!finiteCheck::isFinite(gravity))
  {
    return;
  }

  m_gravity.set(gravity);
}

bool RigidBody::getDoGravity() const
{
  return m_doGravity.get();
}

void RigidBody::setDoGravity(const bool doGravity)
{
  m_doGravity.set(doGravity);
}

bool RigidBody::isFalling() const
{
  return m_falling;
}

void RigidBody::setFalling(const bool falling)
{
  m_falling = falling;
}

bool RigidBody::getNextFalling() const
{
  return m_nextFalling;
}

void RigidBody::setNextFalling(const bool nextFalling)
{
  m_nextFalling = nextFalling;
}

float RigidBody::getStackedLoad() const
{
  return m_stackedLoad;
}

void RigidBody::setStackedLoad(const float stackedLoad)
{
  m_stackedLoad = stackedLoad;
}

glm::vec3 RigidBody::getHeldImpulse() const
{
  return m_heldImpulse;
}

void RigidBody::setHeldImpulse(const glm::vec3& heldImpulse)
{
  m_heldImpulse = heldImpulse;
}

nlohmann::json RigidBody::serialize()
{
  const auto velocity = m_velocity.getInitialValue();
  const auto angularVelocity = m_angularVelocity.getInitialValue();

  const nlohmann::json data = {
    { "type", "RigidBody" },
    { "velocity", { velocity.x, velocity.y, velocity.z } },
    { "angularVelocity", { angularVelocity.x, angularVelocity.y, angularVelocity.z } },
    { "friction", m_friction.getInitialValue() },
    { "doGravity", m_doGravity.getInitialValue() },
    { "gravity", m_gravity.getInitialValue() },
    { "mass", m_mass.getInitialValue() }
  };

  return data;
}

void RigidBody::loadFromJSON(const nlohmann::json& componentData)
{
  const auto& velocity = componentData.at("velocity");
  m_velocity.set(glm::vec3(
    velocity.at(0),
    velocity.at(1),
    velocity.at(2)
  ));

  const auto& angularVelocity = componentData.at("angularVelocity");
  m_angularVelocity.set(glm::vec3(
    angularVelocity.at(0),
    angularVelocity.at(1),
    angularVelocity.at(2)
  ));

  m_friction.set(componentData.at("friction"));
  m_doGravity.set(componentData.at("doGravity"));
  m_gravity.set(componentData.at("gravity"));
  setMass(componentData.at("mass"));
}

void RigidBody::pack(net::Message& message) const
{
  message.write(ComponentType::rigidBody);

  message.write(m_velocity.get());
  message.write(m_friction.get());
  message.write(m_doGravity.get());
  message.write(m_gravity.get());
  message.write(m_angularVelocity.get());
  message.write(m_mass.get());
}

void RigidBody::unpack(net::MessageReader& messageReader)
{
  m_velocity.set(messageReader.read<glm::vec3>());
  m_friction.set(messageReader.read<float>());
  m_doGravity.set(messageReader.read<bool>());
  m_gravity.set(messageReader.read<float>());
  m_angularVelocity.set(messageReader.read<glm::vec3>());
  setMass(messageReader.read<float>());
}
