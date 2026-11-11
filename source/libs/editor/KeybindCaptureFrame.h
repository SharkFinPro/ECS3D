#ifndef KEYBINDCAPTUREFRAME_H
#define KEYBINDCAPTUREFRAME_H

#include "KeybindRebindFlow.h"
#include <utility>

// Per-frame bookkeeping for the Settings panel's key capture: whether the Keybinds list was drawn this
// frame, and the reconcile every frame ends with so no key event can reach a capture the list outlived.
class KeybindCaptureFrame {
public:
  void drawn()
  {
    m_drawn = true;
  }

  // The list is going away this frame (window closed, GUI hidden), even if it was already drawn.
  void hidden()
  {
    m_drawn = false;
  }

  // Ends the frame. Returns true when the dispatcher's capture must be cancelled.
  [[nodiscard]] bool endFrame(KeybindRebindFlow& flow, const bool dispatcherCapturing)
  {
    return flow.reconcileCapture(dispatcherCapturing, std::exchange(m_drawn, false));
  }

private:
  bool m_drawn = false;
};

#endif  // KEYBINDCAPTUREFRAME_H
