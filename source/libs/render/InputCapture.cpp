#include "InputCapture.h"
#include "InputFocus.h"
#include <imgui.h>
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/window/Window.h>
#include <GLFW/glfw3.h>

namespace input {

namespace {

// With ImGui multi-viewport on, detached panels are their own GLFW windows (viewports after the main one).
bool detachedWindowFocused()
{
  if (!ImGui::GetCurrentContext() || !(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable))
  {
    return false;
  }

  const auto& viewports = ImGui::GetPlatformIO().Viewports;
  for (int i = 1; i < viewports.Size; ++i)
  {
    const auto handle = static_cast<GLFWwindow*>(viewports[i]->PlatformHandle);
    if (handle && glfwGetWindowAttrib(handle, GLFW_FOCUSED) == GLFW_TRUE)
    {
      return true;
    }
  }

  return false;
}

}

InputSnapshot capture(const vke::VulkanEngine& renderer)
{
  InputSnapshot snapshot;

  const auto window = renderer.getWindow();
  if (!window)
  {
    return snapshot;
  }

  const bool focused = anyWindowFocused(glfwGetWindowAttrib(window->getWindow(), GLFW_FOCUSED) == GLFW_TRUE,
                                        detachedWindowFocused());

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

  applyFocus(snapshot, focused);

  return snapshot;
}

}
