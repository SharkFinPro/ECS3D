#include "RigidBody.h"
#include "Transform.h"
#include "../Object.h"
#include "../../ECS3D.h"
#include <imgui.h>
#include <glm/glm.hpp>

RigidBody::RigidBody()
  : Component(ComponentType::rigidBody),
    m_velocity(glm::vec3(0)),
    m_friction(0.1f),
    m_doGravity(true),
    m_gravity(-GRAVITY),
    m_angularVelocity(glm::vec3(0)),
    falling(true), nextFalling(true)
{
  loadVariable(m_velocity);
  loadVariable(m_friction);
  loadVariable(m_doGravity);
  loadVariable(m_gravity);
  loadVariable(m_angularVelocity);
}

void RigidBody::variableUpdate(float dt)
{
  const auto renderer = getOwner()->getManager()->getECS()->getRenderer();

  for (const auto&[start, end] : linesToDraw)
  {
    renderer->renderLine(start, end);
  }
}

void RigidBody::fixedUpdate(const float dt)
{
  linesToDraw.clear();

  if (transform_ptr.expired())
  {
    transform_ptr = owner->getComponent<Transform>(ComponentType::transform);

    if (transform_ptr.expired())
    {
      return;
    }
  }

  if (const std::shared_ptr<Transform> transform = transform_ptr.lock())
  {
    falling = nextFalling;
    nextFalling = true;

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

    m_angularVelocity.value() *= 0.85f;

    float angularSpeed = glm::length(m_angularVelocity.get());

    // Stop very small angular velocities to prevent jitter
    if (angularSpeed < 0.01f)
    {
      m_angularVelocity.set(glm::vec3(0.0f));
    }
  }
}

void RigidBody::applyForce(const glm::vec3& force, const glm::vec3& position)
{
  m_velocity.value() += force;

  if (m_velocity.get().y > 0 && m_velocity.get().y - force.y < 0)
  {
    falling = true;
    nextFalling = true;
  }

  auto r = position - transform_ptr.lock()->getPosition();

  if (glm::length(r) > 0.01f)
  {
    const auto angularImpulse = glm::cross(r, force);
    m_angularVelocity.value() += angularImpulse * 200.0f;
  }
}

void RigidBody::handleCollision(const glm::vec3 minimumTranslationVector, const std::shared_ptr<Object>& other,
                                const glm::vec3 collisionPoint)
{
  if (!other)
  {
    throw std::runtime_error("RigidBody::handleCollision missing other object!");
  }

  linesToDraw.emplace_back(collisionPoint, transform_ptr.lock()->getPosition());

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
    falling = false;
    nextFalling = false;
  }

  if (transform_ptr.expired())
  {
    transform_ptr = owner->getComponent<Transform>(ComponentType::transform);

    if (transform_ptr.expired())
    {
      return;
    }
  }

  if (const std::shared_ptr<Transform> transform = transform_ptr.lock())
  {
    transform->move(minimumTranslationVector);
  }
}

bool RigidBody::isFalling() const
{
  return falling;
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
  }
}

void RigidBody::limitMovement()
{
  if (glm::length(m_velocity.get()) < 1e-5f)
  {
    return;
  }

  const glm::vec2 horizontalVelocity(m_velocity.get().x, m_velocity.get().z);
  const glm::vec2 frictionForce = -horizontalVelocity * m_friction.get();

  applyForce({ frictionForce.x, 0.0f, frictionForce.y }, transform_ptr.lock()->getPosition());
}
