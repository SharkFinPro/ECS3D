#ifndef INPUTVIEW_H
#define INPUTVIEW_H

#include "InputCapture.h"

#include <optional>
#include <uuid.h>

namespace input {

// The scene view's rectangle in the same coordinate space as the cursor passed to setView.
struct ViewRect {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

// Fills the snapshot's view block: the cursor relative to the rect's top-left corner, the rect's size and
// the scene camera object the view looks through (none leaves the camera empty).
void setView(InputSnapshot& snapshot, float cursorX, float cursorY, const ViewRect& rect,
             const std::optional<uuids::uuid>& camera);

}

#endif //INPUTVIEW_H
