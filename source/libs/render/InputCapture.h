#ifndef INPUTCAPTURE_H
#define INPUTCAPTURE_H

#include <cstdint>
#include <vector>

namespace vke {
  class VulkanEngine;
}

// Captures the local keyboard + mouse so a client/editor can ship it to the authoritative server as an
// inputState message (the server is headless and has no window of its own). Lives in ECS3DRender
// because it reads the renderer's GLFW window.
namespace input {

// Mouse button bits, matching GLFW_MOUSE_BUTTON_{LEFT,RIGHT,MIDDLE} (0/1/2) via (1 << button).
enum MouseButtonBit : uint8_t {
  mouseButtonLeft   = 1u << 0,
  mouseButtonRight  = 1u << 1,
  mouseButtonMiddle = 1u << 2
};

struct InputSnapshot {
  std::vector<int> keys;   // currently-pressed key codes (GLFW), as the scripts' Key enum expects
  bool focused = true;     // the window only reports keys while it has focus, so this is informational

  // Mouse. Position is absolute (window pixels); delta is this frame's movement (cursor - previous
  // cursor); scroll is this frame's vertical wheel offset. buttons is a MouseButtonBit mask.
  float mouseX = 0.0f;
  float mouseY = 0.0f;
  float mouseDeltaX = 0.0f;
  float mouseDeltaY = 0.0f;
  float scrollY = 0.0f;
  uint8_t buttons = 0;
};

[[nodiscard]] InputSnapshot capture(const vke::VulkanEngine& renderer);

}



#endif //INPUTCAPTURE_H
