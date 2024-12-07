#include "Player.h"

#include <iostream>

#include "../Object.h"
#include "../ObjectManager.h"
#include "../../ECS3D.h"
#include "RigidBody.h"
#include "Transform.h"

Player::Player()
  : Component(ComponentType::player), speed(1.0f), jumpHeight(15.0f), appliedForce(0)
{}

void Player::variableUpdate([[maybe_unused]] const float dt)
{
  handleInput();
}

void Player::fixedUpdate([[maybe_unused]] const float dt)
{
  if (transform_ptr.expired())
  {
    transform_ptr = dynamic_pointer_cast<Transform>(owner->getComponent(ComponentType::transform));

    if (transform_ptr.expired())
    {
      return;
    }
  }

  if (rigidBody_ptr.expired())
  {
    rigidBody_ptr = dynamic_pointer_cast<RigidBody>(getOwner()->getComponent(ComponentType::rigidBody));

    if (rigidBody_ptr.expired())
    {
      return;
    }
  }


  if (const std::shared_ptr<Transform> transform = transform_ptr.lock())
  {
    if (const std::shared_ptr<RigidBody> rigidBody = rigidBody_ptr.lock())
    {
      if (transform->getPosition().y < -250.0f)
      {
        transform->reset();
        rigidBody->setVelocity({0, 0, 0});
      }

      rigidBody->applyForce(appliedForce * dt);
    }
  }

  appliedForce *= 0;
}

void Player::handleInput()
{
  if (rigidBody_ptr.expired())
  {
    rigidBody_ptr = dynamic_pointer_cast<RigidBody>(getOwner()->getComponent(ComponentType::rigidBody));

    if (rigidBody_ptr.expired())
    {
      return;
    }
  }

  const auto ecs = owner->getManager()->getECS();

  float xForce = 0;
  if (ecs->keyIsPressed(GLFW_KEY_LEFT))
  {
    xForce += speed;
  }
  if (ecs->keyIsPressed(GLFW_KEY_RIGHT))
  {
    xForce -= speed;
  }
  if (xForce!= 0)
  {
    appliedForce.x = xForce;
  }

  float zForce = 0;
  if (ecs->keyIsPressed(GLFW_KEY_UP))
  {
    zForce += speed;
  }
  if (ecs->keyIsPressed(GLFW_KEY_DOWN))
  {
    zForce -= speed;
  }
  if (zForce != 0)
  {
    appliedForce.z = zForce;
  }

  if (const std::shared_ptr<RigidBody> rigidBody = rigidBody_ptr.lock())
  {
    if (!rigidBody->isFalling() && ecs->keyIsPressed(GLFW_KEY_X))
    {
      appliedForce.y = jumpHeight;
    }
  }
}
