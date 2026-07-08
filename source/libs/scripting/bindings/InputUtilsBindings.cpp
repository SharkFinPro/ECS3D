#include "InputUtilsBindings.h"
#include "InputState.h"

InputUtilsBindings InputUtilsBindingsProvider::getBindings()
{
  return InputUtilsBindings {
    .keyIsPressed = &bindKeyIsPressed,
    .windowIsFocused = &bindWindowIsFocused
  };
}

bool InputUtilsBindingsProvider::bindKeyIsPressed(const int key)
{
  // Player-agnostic for now: a key reads as pressed if any connection presses it. Phase 3.2 gives a
  // script its own player identity and switches this to the per-connection query.
  return InputState::isAnyKeyPressed(key);
}

bool InputUtilsBindingsProvider::bindWindowIsFocused()
{
  return InputState::isAnyFocused();
}
