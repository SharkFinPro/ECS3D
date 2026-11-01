#include "InputFocus.h"

namespace input {

bool anyWindowFocused(const bool mainFocused, const bool detachedFocused)
{
  return mainFocused || detachedFocused;
}

void applyFocus(InputSnapshot& snapshot, const bool focused)
{
  snapshot.focused = focused;

  if (!focused)
  {
    snapshot.mouseDeltaX = 0.0f;
    snapshot.mouseDeltaY = 0.0f;
    snapshot.scrollY = 0.0f;
    snapshot.buttons = 0;
  }
}

}
