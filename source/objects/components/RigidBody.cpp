#include "RigidBody.h"
#include "Transform.h"
#include "../Object.h"
#include <imgui.h>
#include <glm/glm.hpp>

RigidBody::RigidBody()
  : Component(ComponentType::rigidBody),
    initialVelocity(0), liveVelocity(initialVelocity), currentVelocity(&initialVelocity),
    initialFriction(0.1f), liveFriction(initialFriction), currentFriction(&initialFriction),
    initialDoGravity(true), liveDoGravity(initialDoGravity), currentDoGravity(&initialDoGravity),
    initialGravity(0, -GRAVITY, 0), liveGravity(initialGravity), currentGravity(&initialGravity),
    falling(true), nextFalling(true)
{}

void RigidBody::fixedUpdate(const float dt)
{
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

    if (*currentDoGravity)
    {
      applyForce(*currentGravity * dt * 0.1f);
    }

    limitMovement();

    transform->move(*currentVelocity);
  }
}

void RigidBody::applyForce(const glm::vec3& force)
{
  *currentVelocity += force;

  if (currentVelocity->y > 0 && currentVelocity->y - force.y < 0)
  {
    falling = true;
    nextFalling = true;
  }
}

void RigidBody::handleCollision(const glm::vec3 minimumTranslationVector, const std::shared_ptr<Object>& other)
{
  respondToCollision(minimumTranslationVector);

  if (!other)
  {
    return;
  }

  const auto collisionNormal = normalize(minimumTranslationVector);

  const auto otherRb = other->getComponent<RigidBody>(ComponentType::rigidBody);

  if (!otherRb)
  {
    const auto impulse = dot(-*currentVelocity, collisionNormal) * collisionNormal;
    applyForce(impulse);

    return;
  }

  otherRb->respondToCollision(-minimumTranslationVector);

  const auto velocityDiff = *otherRb->currentVelocity - *currentVelocity;

  if (dot(velocityDiff, collisionNormal) > 0)
  {
    const auto impulse = dot(velocityDiff, collisionNormal) * collisionNormal;
    applyForce(impulse);
    otherRb->applyForce(-impulse);
  }
}

void RigidBody::respondToCollision(const glm::vec3 minimumTranslationVector)
{
  if (minimumTranslationVector.y > 1e-5f && currentVelocity->y <= 1e-5f)
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

void RigidBody::setVelocity(const glm::vec3& velocity) const
{
  *currentVelocity = velocity;
}

void RigidBody::displayGui()
{
  if (displayGuiHeader())
  {
    float newGravity = currentGravity->y;

    ImGui::Checkbox("Do Gravity", currentDoGravity);
    ImGui::InputFloat("Gravity", &newGravity);

    currentGravity->y = newGravity;

    ImGui::SliderFloat("Friction", currentFriction, 0.001f, 1.0f);
  }
}

void RigidBody::start()
{
  liveVelocity = initialVelocity;
  liveFriction = initialFriction;
  liveDoGravity = initialDoGravity;
  liveGravity = initialGravity;

  currentVelocity = &liveVelocity;
  currentFriction = &liveFriction;
  currentDoGravity = &liveDoGravity;
  currentGravity = &liveGravity;
}

void RigidBody::stop()
{
  currentVelocity = &initialVelocity;
  currentFriction = &initialFriction;
  currentDoGravity = &initialDoGravity;
  currentGravity = &initialGravity;
}

void RigidBody::limitMovement()
{
  if (length(*currentVelocity) < 1e-5f)
  {
    return;
  }

  const glm::vec2 horizontalVelocity(currentVelocity->x, currentVelocity->z);
  const glm::vec2 frictionForce = -horizontalVelocity * *currentFriction;

  applyForce({ frictionForce.x, 0.0f, frictionForce.y });
}
