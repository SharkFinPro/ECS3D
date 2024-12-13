#include "RigidBody.h"

#include <cmath>

#include "../Object.h"
#include "Transform.h"

RigidBody::RigidBody()
  : Component(ComponentType::rigidBody), velocity(0, 0, 0), doGravity(true), gravity(0, -9.81f, 0),
    falling(doGravity), wasFalling(doGravity), wasWasFalling(doGravity)
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
    wasWasFalling = wasFalling;

    wasFalling = true;

    falling = true;

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
    if (minimumTranslationVector.y > 0 && minimumTranslationVector.y > 0.001f)
    {
      if (!wasWasFalling)
      {
        falling = false;
      }
      else
      {
        wasFalling = false;
      }
    }

    if (other != nullptr)
    {
      if (const auto otherRb = dynamic_pointer_cast<RigidBody>(other->getComponent(ComponentType::rigidBody)))
      {
        otherRb->handleCollision(-minimumTranslationVector, nullptr);
      }
    }

    velocity += minimumTranslationVector;

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

void RigidBody::limitMovement()
{
  applyForce({ -velocity.x * 0.05f, 0, -velocity.z * 0.05f });
}
