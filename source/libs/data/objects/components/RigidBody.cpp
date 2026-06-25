#include "RigidBody.h"
#include <nlohmann/json.hpp>

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

void RigidBody::addPendingForce(const glm::vec3& force, const glm::vec3& position)
{
  m_pendingForces.push_back({ force, position });
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
  m_velocity.set(velocity);
}

glm::vec3 RigidBody::getAngularVelocity() const
{
  return m_angularVelocity.get();
}

void RigidBody::setAngularVelocity(const glm::vec3& angularVelocity)
{
  m_angularVelocity.set(angularVelocity);
}

float RigidBody::getMass() const
{
  return m_mass.get();
}

void RigidBody::setMass(const float mass)
{
  m_mass.set(mass);
}

float RigidBody::getFriction() const
{
  return m_friction.get();
}

void RigidBody::setFriction(const float friction)
{
  m_friction.set(friction);
}

float RigidBody::getGravity() const
{
  return m_gravity.get();
}

void RigidBody::setGravity(const float gravity)
{
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
  m_mass.set(componentData.at("mass"));
}
