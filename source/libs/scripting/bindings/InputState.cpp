#include "InputState.h"

std::unordered_map<int32_t, InputState::SlotInput> InputState::s_slots;

void InputState::setKeysPressed(const int32_t playerSlot, const std::vector<int>& keys)
{
  auto& slot = s_slots[playerSlot];
  slot.pressedKeys.clear();
  slot.pressedKeys.insert(keys.begin(), keys.end());
}

void InputState::setFocused(const int32_t playerSlot, const bool focused)
{
  s_slots[playerSlot].focused = focused;
}

void InputState::removeSlot(const int32_t playerSlot)
{
  s_slots.erase(playerSlot);
}

bool InputState::isKeyPressed(const int32_t playerSlot, const int key)
{
  const auto it = s_slots.find(playerSlot);
  return it != s_slots.end() && it->second.pressedKeys.contains(key);
}

bool InputState::isFocused(const int32_t playerSlot)
{
  const auto it = s_slots.find(playerSlot);
  return it != s_slots.end() && it->second.focused;
}

bool InputState::isAnyKeyPressed(const int key)
{
  for (const auto& [slot, input] : s_slots)
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
  for (const auto& [slot, input] : s_slots)
  {
    if (input.focused)
    {
      return true;
    }
  }

  return false;
}
