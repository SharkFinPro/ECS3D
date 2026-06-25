#ifndef INPUTUTILSBINDINGS_H
#define INPUTUTILSBINDINGS_H

// Mirrors the C# InputUtilsBindings struct layout (ScriptBridge/components/InputUtils.cs). The managed
// InputUtils calls these function pointers; they now read the networked InputState instead of a local
// GLFW window, since the server is headless.
struct InputUtilsBindings
{
  bool(*keyIsPressed)(int key);
  bool(*windowIsFocused)();
};

class InputUtilsBindingsProvider {
public:
  [[nodiscard]] static InputUtilsBindings getBindings();

private:
  static bool bindKeyIsPressed(int key);

  static bool bindWindowIsFocused();
};



#endif //INPUTUTILSBINDINGS_H
