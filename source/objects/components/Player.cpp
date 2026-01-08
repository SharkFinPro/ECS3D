#include "Player.h"
#include "../Object.h"
#include "../ObjectManager.h"
#include "../../ECS3D.h"
#include "RigidBody.h"
#include "Transform.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <VulkanEngine/components/renderingManager/RenderingManager.h>

Player::Player()
  : Component(ComponentType::player),
    m_speed(1.0f),
    m_jumpHeight(15.0f)
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
  if (m_transform_ptr.expired())
  {
    m_transform_ptr = m_owner->getComponent<Transform>(ComponentType::transform);

    if (m_transform_ptr.expired())
    {
      return;
    }
  }

  if (m_rigidBody_ptr.expired())
  {
    m_rigidBody_ptr = m_owner->getComponent<RigidBody>(ComponentType::rigidBody);

    if (m_rigidBody_ptr.expired())
    {
      return;
    }
  }

  if (const std::shared_ptr<Transform> transform = m_transform_ptr.lock())
  {
    if (const std::shared_ptr<RigidBody> rigidBody = m_rigidBody_ptr.lock())
    {
      if (transform->getPosition().y < -250.0f)
      {
        transform->stop();
        transform->start();
        rigidBody->setVelocity({0, 0, 0});
      }

      rigidBody->applyForce(m_appliedForce * dt, transform->getPosition());
      transform->move(m_appliedForce * dt);
    }
  }

  m_appliedForce *= 0;
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
  if (m_rigidBody_ptr.expired())
  {
    m_rigidBody_ptr = m_owner->getComponent<RigidBody>(ComponentType::rigidBody);

    if (m_rigidBody_ptr.expired())
    {
      return;
    }
  }

  const auto ecs = m_owner->getManager()->getECS();

  if (!ecs->getRenderer()->getRenderingManager()->isSceneFocused())
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
    m_appliedForce.x = xForce;
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
    m_appliedForce.z = zForce;
  }

  if (const std::shared_ptr<RigidBody> rigidBody = m_rigidBody_ptr.lock())
  {
    if (!rigidBody->isFalling() && ecs->keyIsPressed(GLFW_KEY_X))
    {
      m_appliedForce.y = m_jumpHeight.get();
    }
  }
}
