#include "InputUtils.h"
#include "../ECS3D.h"
#include <VulkanEngine/components/renderingManager/RenderingManager.h>

static ECS3D* s_ecs = nullptr;

void InputUtils::initBindings(ECS3D* ecs)
{
  s_ecs = ecs;
}

InputUtilsBindings InputUtils::getBindings()
{
  return InputUtilsBindings {
    .keyIsPressed = &bindKeyIsPressed,
    .windowIsFocused = &bindWindowIsFocused
  };
}

bool InputUtils::bindKeyIsPressed(const int key)
{
  return s_ecs->keyIsPressed(key);
}

bool InputUtils::bindWindowIsFocused()
{
  return s_ecs->getRenderer()->getRenderingManager()->isSceneFocused();
}
