#include "InputUtils.h"
#include "../ECS3D.h"

static ECS3D* s_ecs = nullptr;

void InputUtils::initBindings(ECS3D* ecs)
{
  s_ecs = ecs;
}

InputUtilsBindings InputUtils::getBindings()
{
  return InputUtilsBindings {
    .keyIsPressed = &bindKeyIsPressed
  };
}

bool InputUtils::bindKeyIsPressed(const int key)
{
  return s_ecs->keyIsPressed(key);
}
