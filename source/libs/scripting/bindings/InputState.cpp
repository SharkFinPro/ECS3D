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

void InputState::setMouse(const int32_t playerSlot, const float x, const float y, const float deltaX,
                          const float deltaY, const float scrollY, const uint8_t buttons)
{
  auto& slot = s_slots[playerSlot];
  slot.mouseX = x;
  slot.mouseY = y;
  slot.buttons = buttons;

  // Accumulate motion between ticks so several sub-frame moves add up instead of overwriting each other.
  slot.mouseDeltaX += deltaX;
  slot.mouseDeltaY += deltaY;
  slot.scrollY += scrollY;
}

void InputState::clearMouseDeltas()
{
  for (auto& [slot, input] : s_slots)
  {
    input.mouseDeltaX = 0.0f;
    input.mouseDeltaY = 0.0f;
    input.scrollY = 0.0f;
  }
}

void InputState::commitInputEdges()
{
  for (auto& [slot, input] : s_slots)
  {
    input.previousKeys = input.pressedKeys;
  }
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

bool InputState::wasKeyPressedThisTick(const int32_t playerSlot, const int key)
{
  const auto it = s_slots.find(playerSlot);
  return it != s_slots.end()
    && it->second.pressedKeys.contains(key)
    && !it->second.previousKeys.contains(key);
}

bool InputState::wasKeyReleasedThisTick(const int32_t playerSlot, const int key)
{
  const auto it = s_slots.find(playerSlot);
  return it != s_slots.end()
    && !it->second.pressedKeys.contains(key)
    && it->second.previousKeys.contains(key);
}

bool InputState::isFocused(const int32_t playerSlot)
{
  const auto it = s_slots.find(playerSlot);
  return it != s_slots.end() && it->second.focused;
}

void InputState::getMousePosition(const int32_t playerSlot, float& x, float& y)
{
  const auto it = s_slots.find(playerSlot);
  x = it != s_slots.end() ? it->second.mouseX : 0.0f;
  y = it != s_slots.end() ? it->second.mouseY : 0.0f;
}

void InputState::getMouseDelta(const int32_t playerSlot, float& deltaX, float& deltaY)
{
  const auto it = s_slots.find(playerSlot);
  deltaX = it != s_slots.end() ? it->second.mouseDeltaX : 0.0f;
  deltaY = it != s_slots.end() ? it->second.mouseDeltaY : 0.0f;
}

float InputState::getScroll(const int32_t playerSlot)
{
  const auto it = s_slots.find(playerSlot);
  return it != s_slots.end() ? it->second.scrollY : 0.0f;
}

bool InputState::isMouseButtonPressed(const int32_t playerSlot, const int button)
{
  if (button < 0 || button > 7)
  {
    return false;
  }

  const auto it = s_slots.find(playerSlot);
  return it != s_slots.end() && (it->second.buttons & (1u << button)) != 0u;
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
