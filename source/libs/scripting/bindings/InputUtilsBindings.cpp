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
  return InputState::isKeyPressed(key);
}

bool InputUtilsBindingsProvider::bindWindowIsFocused()
{
  return InputState::isFocused();
}
