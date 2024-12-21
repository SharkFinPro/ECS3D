#include "RigidBody.h"

#include <cmath>
#include <imgui.h>

#include "../Object.h"
#include "Transform.h"

#include <glm/glm.hpp>

RigidBody::RigidBody()
  : Component(ComponentType::rigidBody), velocity(0), doGravity(true), gravity(0, -GRAVITY, 0),
    falling(true), nextFalling(true)
{}

void RigidBody::fixedUpdate(const float dt)
{
  if (transform_ptr.expired())
  {
    transform_ptr = std::dynamic_pointer_cast<Transform>(owner->getComponent(ComponentType::transform));

    if (transform_ptr.expired())
    {
      return;
    }
  }

  if (const std::shared_ptr<Transform> transform = transform_ptr.lock())
  {
    falling = nextFalling;
    nextFalling = true;

    if (doGravity)
    {
      applyForce(gravity * dt * 0.1f);
    }

    limitMovement();

    transform->move(velocity);
  }
}

void RigidBody::applyForce(const glm::vec3& force)
{
  velocity += force;
}

void RigidBody::handleCollision(const glm::vec3 minimumTranslationVector, const std::shared_ptr<Object>& other)
{
  respondToCollision(minimumTranslationVector);

  if (!other)
  {
    return;
  }

  if (minimumTranslationVector.y > 0.001f)
  {
    falling = false;
    nextFalling = false;
  }

  const auto collisionNormal = normalize(minimumTranslationVector);

  const auto otherRb = dynamic_pointer_cast<RigidBody>(other->getComponent(ComponentType::rigidBody));

  if (!otherRb)
  {
    const auto impulse = dot(-velocity, collisionNormal) * collisionNormal;
    applyForce(impulse);

    return;
  }

  otherRb->respondToCollision(-minimumTranslationVector);

  const auto velocityDiff = otherRb->velocity - velocity;

  if (dot(velocityDiff, collisionNormal) > 0)
  {
    const auto impulse = dot(velocityDiff, collisionNormal) * collisionNormal;
    applyForce(impulse);
    otherRb->applyForce(-impulse);
  }
}

void RigidBody::respondToCollision(const glm::vec3 minimumTranslationVector)
{
  if (transform_ptr.expired())
  {
    transform_ptr = dynamic_pointer_cast<Transform>(owner->getComponent(ComponentType::transform));

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
  this->velocity = velocity;
}

void RigidBody::displayGui()
{
  if (ImGui::CollapsingHeader("Rigid Body"))
  {
    float newGravity = gravity.y;

    ImGui::Checkbox("Do Gravity", &doGravity);
    ImGui::InputFloat("Gravity", &newGravity);

    gravity.y = newGravity;

    if (ImGui::Button("Reset"))
    {
      reset();
    }
  }
}

void RigidBody::reset()
{
  velocity = glm::vec3(0);
  gravity = glm::vec3(0, -GRAVITY, 0);
  doGravity = true;
}

void RigidBody::limitMovement()
{
  applyForce({ -velocity.x * 0.05f, 0, -velocity.z * 0.05f });
}
