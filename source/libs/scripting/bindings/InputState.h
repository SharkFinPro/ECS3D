#ifndef INPUTSTATE_H
#define INPUTSTATE_H

#include <unordered_set>
#include <vector>

// The networked input the headless server feeds to the scripts. A client/editor (which owns the GLFW
// window) captures the pressed keys each frame and sends them as an inputState message; ServerApp
// writes them here, and the InputUtils bindings read them. Set + read happen on the server's main
// loop thread (the inputState message is drained and the scripts run on the same thread), so no
// locking is needed.
class InputState {
public:
  static void setKeysPressed(const std::vector<int>& keys);

  static void setFocused(bool focused);

  [[nodiscard]] static bool isKeyPressed(int key);

  [[nodiscard]] static bool isFocused();

private:
  static std::unordered_set<int> s_pressedKeys;
  static bool s_focused;
};



#endif //INPUTSTATE_H
