#include "InputState.h"

std::unordered_set<int> InputState::s_pressedKeys;
bool InputState::s_focused = false;

void InputState::setKeysPressed(const std::vector<int>& keys)
{
  s_pressedKeys.clear();
  s_pressedKeys.insert(keys.begin(), keys.end());
}

void InputState::setFocused(const bool focused)
{
  s_focused = focused;
}

bool InputState::isKeyPressed(const int key)
{
  return s_pressedKeys.contains(key);
}

bool InputState::isFocused()
{
  return s_focused;
}
