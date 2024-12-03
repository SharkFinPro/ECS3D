#include "RigidBody.h"

#include "../Object.h"
#include "Transform.h"

RigidBody::RigidBody()
  : Component(ComponentType::rigidBody), velocity(0, 0, 0), doGravity(true), gravity(0, -9.81f, 0)
{}

void RigidBody::fixedUpdate(float dt)
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
    if (doGravity)
    {
      // TODO: Find a way to avoid scaling this, currently in for testing purposes
      applyForce(gravity * dt * 0.001f);
    }

    transform->move(velocity);
  }
}

void RigidBody::applyForce(const glm::vec3& force)
{
  velocity += force;
}
