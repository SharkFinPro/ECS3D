#ifndef ECS3D_INPUTUTILS_H
#define ECS3D_INPUTUTILS_H

class ECS3D;

struct InputUtilsBindings
{
  bool(*keyIsPressed)(int key);
  bool(*windowIsFocused)();
};

class InputUtils {
public:
  static void initBindings(ECS3D* ecs);

  [[nodiscard]] static InputUtilsBindings getBindings();

private:
  static bool bindKeyIsPressed(int key);

  static bool bindWindowIsFocused();
};


#endif //ECS3D_INPUTUTILS_H