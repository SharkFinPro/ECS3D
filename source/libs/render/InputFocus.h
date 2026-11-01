#ifndef INPUTFOCUS_H
#define INPUTFOCUS_H

#include "InputCapture.h"

namespace input {

// The app has focus when its main window or any detached panel window does.
[[nodiscard]] bool anyWindowFocused(bool mainFocused, bool detachedFocused);

// GLFW reports the cursor, buttons and scroll while a window is unfocused; an unfocused capture must not
// carry them. Keys are left alone because the window only reports them while it has focus.
void applyFocus(InputSnapshot& snapshot, bool focused);

}

#endif //INPUTFOCUS_H
