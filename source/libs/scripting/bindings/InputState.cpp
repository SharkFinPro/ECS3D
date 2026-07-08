#include "InputState.h"

std::unordered_map<int32_t, InputState::ConnectionInput> InputState::s_connections;

void InputState::setKeysPressed(const int32_t connectionId, const std::vector<int>& keys)
{
  auto& slot = s_connections[connectionId];
  slot.pressedKeys.clear();
  slot.pressedKeys.insert(keys.begin(), keys.end());
}

void InputState::setFocused(const int32_t connectionId, const bool focused)
{
  s_connections[connectionId].focused = focused;
}

bool InputState::isKeyPressed(const int32_t connectionId, const int key)
{
  const auto it = s_connections.find(connectionId);
  return it != s_connections.end() && it->second.pressedKeys.contains(key);
}

bool InputState::isFocused(const int32_t connectionId)
{
  const auto it = s_connections.find(connectionId);
  return it != s_connections.end() && it->second.focused;
}

bool InputState::isAnyKeyPressed(const int key)
{
  for (const auto& [id, input] : s_connections)
  {
    if (input.pressedKeys.contains(key))
    {
      return true;
    }
  }

  return false;
}

bool InputState::isAnyFocused()
{
  for (const auto& [id, input] : s_connections)
  {
    if (input.focused)
    {
      return true;
    }
  }

  return false;
}
