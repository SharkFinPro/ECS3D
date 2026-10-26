#include "InputView.h"

namespace input {

void setView(InputSnapshot& snapshot, const float cursorX, const float cursorY, const ViewRect& rect,
             const std::optional<uuids::uuid>& camera)
{
  snapshot.view.camera = camera ? uuids::to_string(*camera) : std::string();
  snapshot.view.cursorX = cursorX - rect.x;
  snapshot.view.cursorY = cursorY - rect.y;
  snapshot.view.width = rect.width;
  snapshot.view.height = rect.height;
}

}
