#include "Player.h"
#include "../Object.h"
#include "../ObjectManager.h"
#include "../../ECS3D.h"
#include "RigidBody.h"
#include "Transform.h"

Player::Player()
  : Component(ComponentType::player),
    initialSpeed(1.0f), liveSpeed(initialSpeed), currentSpeed(&initialSpeed),
    initialJumpHeight(9.0f), liveJumpHeight(initialJumpHeight), currentJumpHeight(&initialJumpHeight),
    initialAppliedForce(0), liveAppliedForce(initialAppliedForce), currentAppliedForce(&initialAppliedForce)
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
        // TODO: Reset Transform position
        rigidBody->setVelocity({0, 0, 0});
      }

      rigidBody->applyForce(*currentAppliedForce * dt);
    }
  }

  *currentAppliedForce *= 0;
}

void Player::displayGui()
{
  if (displayGuiHeader())
  {
    ImGui::InputFloat("Speed", currentSpeed);
    ImGui::InputFloat("Jump Height", currentJumpHeight);
  }
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
    xForce += *currentSpeed;
  }
  if (ecs->keyIsPressed(GLFW_KEY_RIGHT))
  {
    xForce -= *currentSpeed;
  }
  if (xForce!= 0)
  {
    currentAppliedForce->x = xForce;
  }

  float zForce = 0;
  if (ecs->keyIsPressed(GLFW_KEY_UP))
  {
    zForce += *currentSpeed;
  }
  if (ecs->keyIsPressed(GLFW_KEY_DOWN))
  {
    zForce -= *currentSpeed;
  }
  if (zForce != 0)
  {
    currentAppliedForce->z = zForce;
  }

  if (const std::shared_ptr<RigidBody> rigidBody = rigidBody_ptr.lock())
  {
    if (!rigidBody->isFalling() && ecs->keyIsPressed(GLFW_KEY_X))
    {
      currentAppliedForce->y = *currentJumpHeight;
    }
  }
}
