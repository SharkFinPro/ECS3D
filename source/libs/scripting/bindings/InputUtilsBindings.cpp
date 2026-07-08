#include "InputUtilsBindings.h"
#include "InputState.h"
#include "BindingContext.h"
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/components/PlayerController.h>
#include <optional>
#include <string>

namespace {
  // Resolve an object's player slot from its PlayerController. Returns nullopt when the object doesn't
  // exist or carries no PlayerController (so per-player input reads as "nothing pressed").
  std::optional<int32_t> playerSlotOf(const char* uuid)
  {
    const auto objectManager = BindingContext::getObjectManager();
    if (!objectManager)
    {
      return std::nullopt;
    }

    const auto parsed = uuids::uuid::from_string(std::string(uuid));
    if (!parsed.has_value())
    {
      return std::nullopt;
    }

    const auto object = objectManager->getObjectByUUID(parsed.value());
    if (!object)
    {
      return std::nullopt;
    }

    const auto playerController = object->getComponent<PlayerController>(ComponentType::playerController);
    if (!playerController)
    {
      return std::nullopt;
    }

    return playerController->getPlayerSlot();
  }
}

InputUtilsBindings InputUtilsBindingsProvider::getBindings()
{
  return InputUtilsBindings {
    .keyIsPressed = &bindKeyIsPressed,
    .windowIsFocused = &bindWindowIsFocused,
    .keyIsPressedForObject = &bindKeyIsPressedForObject,
    .windowIsFocusedForObject = &bindWindowIsFocusedForObject,
    .mousePositionForObject = &bindMousePositionForObject,
    .mouseDeltaForObject = &bindMouseDeltaForObject,
    .scrollForObject = &bindScrollForObject,
    .mouseButtonForObject = &bindMouseButtonForObject
  };
}

bool InputUtilsBindingsProvider::bindKeyIsPressed(const int key)
{
  // Player-agnostic: a key reads as pressed if any player presses it. Scripts that want a specific
  // player's input read through ScriptBase.input (the *ForObject path below).
  return InputState::isAnyKeyPressed(key);
}

bool InputUtilsBindingsProvider::bindWindowIsFocused()
{
  return InputState::isAnyFocused();
}

bool InputUtilsBindingsProvider::bindKeyIsPressedForObject(const char* uuid, const int key)
{
  const auto slot = playerSlotOf(uuid);
  return slot.has_value() && InputState::isKeyPressed(slot.value(), key);
}

bool InputUtilsBindingsProvider::bindWindowIsFocusedForObject(const char* uuid)
{
  const auto slot = playerSlotOf(uuid);
  return slot.has_value() && InputState::isFocused(slot.value());
}

void InputUtilsBindingsProvider::bindMousePositionForObject(const char* uuid, float* x, float* y)
{
  *x = 0.0f;
  *y = 0.0f;

  if (const auto slot = playerSlotOf(uuid))
  {
    InputState::getMousePosition(slot.value(), *x, *y);
  }
}

void InputUtilsBindingsProvider::bindMouseDeltaForObject(const char* uuid, float* x, float* y)
{
  *x = 0.0f;
  *y = 0.0f;

  if (const auto slot = playerSlotOf(uuid))
  {
    InputState::getMouseDelta(slot.value(), *x, *y);
  }
}

float InputUtilsBindingsProvider::bindScrollForObject(const char* uuid)
{
  const auto slot = playerSlotOf(uuid);
  return slot.has_value() ? InputState::getScroll(slot.value()) : 0.0f;
}

bool InputUtilsBindingsProvider::bindMouseButtonForObject(const char* uuid, const int button)
{
  const auto slot = playerSlotOf(uuid);
  return slot.has_value() && InputState::isMouseButtonPressed(slot.value(), button);
}
