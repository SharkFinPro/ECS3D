#include "Player.h"
#include "../Object.h"
#include "../ObjectManager.h"
#include "../../ECS3D.h"
#include "RigidBody.h"
#include "Transform.h"

Player::Player()
  : Component(ComponentType::player), initialSpeed(1.0f), speed(initialSpeed),
    initialJumpHeight(9.0f), jumpHeight(initialJumpHeight), appliedForce(0)
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

void Player::displayGui()
{
  if (ImGui::CollapsingHeader("Player"))
  {
    ImGui::InputFloat("Speed", &speed);
    ImGui::InputFloat("Jump Height", &jumpHeight);

    if (ImGui::Button("Reset"))
    {
      reset();
    }
  }
}

void Player::reset()
{
  speed = initialSpeed;
  jumpHeight = initialJumpHeight;
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

  if (!ecs->getRenderer()->sceneIsFocused())
  {
    return;
  }

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
