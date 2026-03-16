#include "ScriptBindings.h"
#include "../objects/components/Transform.h"

using RegisterTransformFn = void(*)(TransformBindings);

void ScriptBindings::init(ECS3D* ecs)
{
  Transform::initBindings(ecs);
}

void ScriptBindings::registerAll(const std::function<void(const char*, void**)>& loadFn)
{
  RegisterTransformFn fn = nullptr;
  loadFn("registerTransformBindings", reinterpret_cast<void**>(&fn));
  fn(Transform::getBindings());
}