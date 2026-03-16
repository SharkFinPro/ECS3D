#include "ScriptBindings.h"

static float getPositionX()
{
  return 21.0f;
}

void ScriptBindings::init() {}

void ScriptBindings::registerAll(const std::function<void(const char*, void**)>& loadFn)
{
  RegisterTransformFn fn = nullptr;
  loadFn("registerTransformBindings", reinterpret_cast<void**>(&fn));
  fn(TransformBindings {
    .getPositionX = &getPositionX,
  });
}