#include "InputCapture.h"
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/window/Window.h>
#include <GLFW/glfw3.h>

namespace input {

InputSnapshot capture(const vke::VulkanEngine& renderer)
{
  InputSnapshot snapshot;

  const auto window = renderer.getWindow();
  if (!window)
  {
    return snapshot;
  }

  snapshot.focused = glfwGetWindowAttrib(window->getWindow(), GLFW_FOCUSED) == GLFW_TRUE;

  // Poll the keys the ScriptBridge Key enum exposes: space, the arrow keys, and A-Z. The window only
  // reports a key as pressed while it has focus, so this naturally goes quiet when the user tabs away.
  const auto poll = [&](const int low, const int high) {
    for (int key = low; key <= high; ++key)
    {
      if (window->keyIsPressed(key))
      {
        snapshot.keys.push_back(key);
      }
    }
  };

  poll(32, 32);    // space
  poll(65, 90);    // A-Z
  poll(262, 265);  // right/left/down/up arrows

  // Mouse: absolute cursor position, this frame's movement (current - previous cursor, both cached by
  // the window each frame), the vertical scroll accumulated this frame, and the button state as a mask.
  double cursorX = 0.0, cursorY = 0.0;
  window->getCursorPos(cursorX, cursorY);
  snapshot.mouseX = static_cast<float>(cursorX);
  snapshot.mouseY = static_cast<float>(cursorY);

  double prevX = 0.0, prevY = 0.0;
  window->getPreviousCursorPos(prevX, prevY);
  snapshot.mouseDeltaX = static_cast<float>(cursorX - prevX);
  snapshot.mouseDeltaY = static_cast<float>(cursorY - prevY);

  snapshot.scrollY = static_cast<float>(window->getScroll());

  // GLFW_MOUSE_BUTTON_{LEFT,RIGHT,MIDDLE} are 0/1/2; pack them into the MouseButtonBit mask.
  if (window->buttonIsPressed(0)) { snapshot.buttons |= mouseButtonLeft; }
  if (window->buttonIsPressed(1)) { snapshot.buttons |= mouseButtonRight; }
  if (window->buttonIsPressed(2)) { snapshot.buttons |= mouseButtonMiddle; }

  // GLFW still reports the cursor and buttons while the window is unfocused; without this an unfocused
  // window would keep steering the player's look.
  if (!snapshot.focused)
  {
    snapshot.mouseDeltaX = 0.0f;
    snapshot.mouseDeltaY = 0.0f;
    snapshot.scrollY = 0.0f;
    snapshot.buttons = 0;
  }

  // Look motion is a right-drag gesture, as with the free-fly camera: cursor movement with the button up
  // never leaves the client, so no consumer can turn a view from it.
  if ((snapshot.buttons & mouseButtonRight) == 0)
  {
    snapshot.mouseDeltaX = 0.0f;
    snapshot.mouseDeltaY = 0.0f;
  }

  return snapshot;
}

}
