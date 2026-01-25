#include "RigidBody.h"
#include "Transform.h"
#include "../Object.h"
#include "../../ECS3D.h"
#include <glm/glm.hpp>
#include <imgui.h>
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

#ifdef COLLISION_LOCATION_DEBUG
void RigidBody::variableUpdate(float dt)
{
  const auto renderer = getOwner()->getManager()->getECS()->getRenderer();

  for (const auto&[start, end] : linesToDraw)
  {
    renderer->renderLine(start, end);
  }
}
#endif

void RigidBody::fixedUpdate(const float dt)
{
#ifdef COLLISION_LOCATION_DEBUG
  linesToDraw.clear();
#endif

  if (m_transform_ptr.expired())
  {
    m_transform_ptr = m_owner->getComponent<Transform>(ComponentType::transform);

    if (m_transform_ptr.expired())
    {
      return;
    }
  }

  if (const std::shared_ptr<Transform> transform = m_transform_ptr.lock())
  {
    m_falling = m_nextFalling;
    m_nextFalling = true;

    if (m_doGravity.get())
    {
      const glm::vec3 gravity = { 0, m_gravity.get() * dt * 0.1f, 0 };
      applyForce(gravity, transform->getPosition());
    }

    limitMovement();

    transform->move(m_velocity.get());

    const auto rotation = transform->getRotation();
    const auto newRotation = rotation + m_angularVelocity.get() * dt;
    transform->setRotation(newRotation);

    constexpr float damping = 0.99f;
    m_angularVelocity.value() *= damping;
  }
}

void RigidBody::applyForce(const glm::vec3& force, const glm::vec3& position)
{
  m_velocity.value() += force;

  if (m_velocity.get().y > 0 && m_velocity.get().y - force.y < 0)
  {
    m_falling = true;
    m_nextFalling = true;
  }

  const auto r = position - m_transform_ptr.lock()->getPosition();
  if (glm::length(r) <= 0.01f)
  {
    return;
  }

  const auto angularImpulse = glm::cross(r, force);

  m_angularVelocity.value() += angularImpulse / getInertiaTensor();
}

void RigidBody::handleCollision(const glm::vec3 minimumTranslationVector, const std::shared_ptr<Object>& other,
                                const glm::vec3 collisionPoint)
{
  if (!other)
  {
    throw std::runtime_error("RigidBody::handleCollision missing other object!");
  }

#ifdef COLLISION_LOCATION_DEBUG
  linesToDraw.emplace_back(collisionPoint, transform_ptr.lock()->getPosition());
#endif

  respondToCollision(minimumTranslationVector);

  const auto collisionNormal = normalize(minimumTranslationVector);
  const auto otherRb = other->getComponent<RigidBody>(ComponentType::rigidBody);

  if (!otherRb)
  {
    const auto impulse = dot(-m_velocity.get(), collisionNormal) * collisionNormal;
    applyForce(impulse, collisionPoint);

    return;
  }

  otherRb->respondToCollision(-minimumTranslationVector);

  const auto velocityDiff = otherRb->m_velocity.get() - m_velocity.get();

  if (dot(velocityDiff, collisionNormal) <= 0)
  {
    return;
  }

  const auto impulse = dot(velocityDiff, collisionNormal) * collisionNormal;
  applyForce(impulse, collisionPoint);
  otherRb->applyForce(-impulse, collisionPoint);
}

void RigidBody::respondToCollision(const glm::vec3 minimumTranslationVector)
{
  if (minimumTranslationVector.y > 1e-5f && m_velocity.get().y <= 1e-5f)
  {
    m_falling = false;
    m_nextFalling = false;
  }

  if (m_transform_ptr.expired())
  {
    m_transform_ptr = m_owner->getComponent<Transform>(ComponentType::transform);

    if (m_transform_ptr.expired())
    {
      return;
    }
  }

  const auto transform = m_transform_ptr.lock();
  transform->move(minimumTranslationVector);
}

bool RigidBody::isFalling() const
{
  return m_falling;
}

void RigidBody::setVelocity(const glm::vec3& velocity)
{
  m_velocity.set(velocity);
}

void RigidBody::displayGui()
{
  if (displayGuiHeader())
  {
    ImGui::Checkbox("Do Gravity", &m_doGravity.value());
    ImGui::InputFloat("Gravity", &m_gravity.value());

    ImGui::SliderFloat("Friction", &m_friction.value(), 0.001f, 1.0f);

    ImGui::SliderFloat("Mass", &m_mass.value(), 1.0f, 50.0f);
  }
}

nlohmann::json RigidBody::serialize()
{
  const auto velocity = m_velocity.value();
  const auto angularVelocity = m_angularVelocity.value();

  const nlohmann::json data = {
    { "type", "RigidBody" },
    { "velocity", { velocity.x, velocity.y, velocity.z } },
    { "angularVelocity", { angularVelocity.x, angularVelocity.y, angularVelocity.z } },
    { "friction", m_friction.value() },
    { "doGravity", m_doGravity.value() },
    { "gravity", m_gravity.value() },
    { "mass", m_mass.value() }
  };

  return data;
}

void RigidBody::loadFromJSON(const nlohmann::json& componentData)
{
  m_velocity.set(glm::vec3(
    componentData["velocity"][0],
    componentData["velocity"][1],
    componentData["velocity"][2]
  ));

  m_angularVelocity.set(glm::vec3(
    componentData["angularVelocity"][0],
    componentData["angularVelocity"][1],
    componentData["angularVelocity"][2]
  ));

  m_friction.set(componentData["friction"]);
  m_doGravity.set(componentData["doGravity"]);
  m_gravity.set(componentData["gravity"]);
  m_mass.set(componentData["mass"]);
}

void RigidBody::limitMovement()
{
  if (glm::length(m_velocity.get()) < 1e-5f)
  {
    return;
  }

  const glm::vec2 horizontalVelocity(m_velocity.get().x, m_velocity.get().z);
  const glm::vec2 frictionForce = -horizontalVelocity * m_friction.get();

  applyForce({ frictionForce.x, 0.0f, frictionForce.y }, m_transform_ptr.lock()->getPosition());
}

glm::mat3x3 RigidBody::getInertiaTensor() const
{
  const auto scale = m_transform_ptr.lock()->getScale();

  const auto widthSquared = scale.x * scale.x;
  const auto heightSquared = scale.y * scale.y;
  const auto depthSquared = scale.z * scale.z;

  const float factor = 1.0f / 12.0f * m_mass.get() * 0.1f;

  const float Ixx = factor * (heightSquared + depthSquared);
  const float Iyy = factor * (widthSquared + depthSquared);
  const float Izz = factor * (widthSquared + heightSquared);

  return {
    Ixx, 0.0f, 0.0f,
    0.0f, Iyy, 0.0f,
    0.0f, 0.0f, Izz
  };
}
