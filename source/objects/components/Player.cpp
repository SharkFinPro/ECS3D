#include "Player.h"
#include "../Object.h"
#include "RigidBody.h"
#include <imgui.h>
#include <nlohmann/json.hpp>

Player::Player()
  : Component(ComponentType::player)
{
  loadVariable(m_speed);
  loadVariable(m_jumpHeight);
}

void Player::displayGui()
{
  if (displayGuiHeader())
  {
    ImGui::InputFloat("Speed", &m_speed.value());
    ImGui::InputFloat("Jump Height", &m_jumpHeight.value());
  }
}

nlohmann::json Player::serialize()
{
  const nlohmann::json data = {
    { "type", "Player" },
    { "speed", m_speed.value() },
    { "jumpHeight", m_jumpHeight.value() }
  };

  return data;
}

void Player::loadFromJSON(const nlohmann::json& componentData)
{
  m_speed.set(componentData.at("speed"));
  m_jumpHeight.set(componentData.at("jumpHeight"));
}
