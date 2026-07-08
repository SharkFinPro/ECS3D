#include "PlayerController.h"
#include <nlohmann/json.hpp>
#include <Protocol.h>

PlayerController::PlayerController()
  : Component(ComponentType::playerController)
{
  loadVariable(m_playerSlot);
}

int32_t PlayerController::getPlayerSlot() const
{
  return m_playerSlot.get();
}

void PlayerController::setPlayerSlot(const int32_t playerSlot)
{
  m_playerSlot.set(playerSlot);
}

nlohmann::json PlayerController::serialize()
{
  return {
    { "type", "PlayerController" },
    { "playerSlot", m_playerSlot.getInitialValue() }
  };
}

void PlayerController::loadFromJSON(const nlohmann::json& componentData)
{
  // value(...) so an older scene without the field defaults cleanly (slot 0).
  m_playerSlot.set(componentData.value("playerSlot", 0));
}

void PlayerController::pack(net::Message& message) const
{
  message.write(ComponentType::playerController);
  message.write(m_playerSlot.get());
}

void PlayerController::unpack(net::MessageReader& messageReader)
{
  m_playerSlot.set(messageReader.read<int32_t>());
}
