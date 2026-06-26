#include "InputCapture.h"
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/window/Window.h>

namespace input {

InputSnapshot capture(const vke::VulkanEngine& renderer)
{
  InputSnapshot snapshot;

  const auto window = renderer.getWindow();
  if (!window)
  {
    return snapshot;
  }

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

  return snapshot;
}

}
