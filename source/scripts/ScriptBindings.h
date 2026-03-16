#ifndef ECS3D_SCRIPTBINDINGS_H
#define ECS3D_SCRIPTBINDINGS_H

#include <functional>

struct TransformBindings
{
  float(*getPositionX)();
};

using RegisterTransformFn = void(*)(TransformBindings);

class ScriptBindings
{
public:
  static void init();
  static void registerAll(const std::function<void(const char*, void**)>& loadFn);
};

#endif //ECS3D_SCRIPTBINDINGS_H