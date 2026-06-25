#ifndef INPUTCAPTURE_H
#define INPUTCAPTURE_H

#include <vector>

namespace vke {
  class VulkanEngine;
}

// Captures the local keyboard so a client/editor can ship it to the authoritative server as an
// inputState message (the server is headless and has no window of its own). Lives in ECS3DRender
// because it reads the renderer's GLFW window.
namespace input {

struct InputSnapshot {
  std::vector<int> keys;   // currently-pressed key codes (GLFW), as the scripts' Key enum expects
  bool focused = true;     // the window only reports keys while it has focus, so this is informational
};

[[nodiscard]] InputSnapshot capture(vke::VulkanEngine& renderer);

}



#endif //INPUTCAPTURE_H
