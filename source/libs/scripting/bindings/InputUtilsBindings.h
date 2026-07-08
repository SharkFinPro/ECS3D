#ifndef INPUTUTILSBINDINGS_H
#define INPUTUTILSBINDINGS_H

// Mirrors the C# InputUtilsBindings struct layout (ScriptBridge/components/InputUtils.cs). The managed
// InputUtils calls these function pointers; they read the networked InputState instead of a local GLFW
// window, since the server is headless.
//
// keyIsPressed/windowIsFocused are the player-agnostic aggregate (any player). The *ForObject variants
// are per-player: given an object uuid they resolve its PlayerController.playerSlot and read that
// player's slot, so a script reads only its own player's input (ScriptBase.input). New fields go at the
// END to keep the layout matched with the C# mirror.
struct InputUtilsBindings
{
  bool(*keyIsPressed)(int key);
  bool(*windowIsFocused)();
  bool(*keyIsPressedForObject)(const char* uuid, int key);
  bool(*windowIsFocusedForObject)(const char* uuid);
};

class InputUtilsBindingsProvider {
public:
  [[nodiscard]] static InputUtilsBindings getBindings();

private:
  static bool bindKeyIsPressed(int key);

  static bool bindWindowIsFocused();

  static bool bindKeyIsPressedForObject(const char* uuid, int key);

  static bool bindWindowIsFocusedForObject(const char* uuid);
};



#endif //INPUTUTILSBINDINGS_H
