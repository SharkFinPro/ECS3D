#ifndef ECS3D_SCRIPTBINDINGS_H
#define ECS3D_SCRIPTBINDINGS_H

#include <functional>

class ECS3D;

class ScriptBindings
{
public:
  static void init(ECS3D* ecs);
  static void registerAll(const std::function<void(const char*, void**)>& loadFn);
};

#endif //ECS3D_SCRIPTBINDINGS_H