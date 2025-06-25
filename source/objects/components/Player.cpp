#include "Player.h"
#include "../Object.h"
#include "../ObjectManager.h"
#include "../../ECS3D.h"
#include "RigidBody.h"
#include "Transform.h"

Player::Player()
  : Component(ComponentType::player),
    m_speed(1.0f),
    m_jumpHeight(15.0f),
    appliedForce(0)
{
  loadVariable(m_speed);
  loadVariable(m_jumpHeight);
}

void Player::variableUpdate([[maybe_unused]] const float dt)
{
  handleInput();
}

void Player::fixedUpdate([[maybe_unused]] const float dt)
{
  if (transform_ptr.expired())
  {
    transform_ptr = owner->getComponent<Transform>(ComponentType::transform);

    if (transform_ptr.expired())
    {
      return;
    }
  }

  if (rigidBody_ptr.expired())
  {
    rigidBody_ptr = owner->getComponent<RigidBody>(ComponentType::rigidBody);

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
        transform->stop();
        transform->start();
        rigidBody->setVelocity({0, 0, 0});
      }

      rigidBody->applyForce(appliedForce * dt, transform->getPosition());
      transform->move(appliedForce * dt);
    }
  }

  appliedForce *= 0;
}

void Player::displayGui()
{
  if (displayGuiHeader())
  {
    ImGui::InputFloat("Speed", &m_speed.value());
    ImGui::InputFloat("Jump Height", &m_jumpHeight.value());
  }
}

void Player::handleInput()
{
  if (rigidBody_ptr.expired())
  {
    rigidBody_ptr = owner->getComponent<RigidBody>(ComponentType::rigidBody);

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
    xForce += m_speed.get();
  }
  if (ecs->keyIsPressed(GLFW_KEY_RIGHT))
  {
    xForce -= m_speed.get();
  }
  if (xForce!= 0)
  {
    appliedForce.x = xForce;
  }

  float zForce = 0;
  if (ecs->keyIsPressed(GLFW_KEY_UP))
  {
    zForce += m_speed.get();
  }
  if (ecs->keyIsPressed(GLFW_KEY_DOWN))
  {
    zForce -= m_speed.get();
  }
  if (zForce != 0)
  {
    appliedForce.z = zForce;
  }

  if (const std::shared_ptr<RigidBody> rigidBody = rigidBody_ptr.lock())
  {
    if (!rigidBody->isFalling() && ecs->keyIsPressed(GLFW_KEY_X))
    {
      appliedForce.y = m_jumpHeight.get();
    }
  }
}
