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

float RigidBody::getFriction() const
{
  return m_friction.get();
}

float RigidBody::getGravity() const
{
  return m_gravity.get();
}

bool RigidBody::getDoGravity() const
{
  return m_doGravity.get();
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
