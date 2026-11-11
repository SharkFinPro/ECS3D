#ifndef KEYCAPTURE_H
#define KEYCAPTURE_H

#include <Keybinds.h>
#include <functional>
#include <utility>

// The dispatcher's pending "next chord goes here" slot, without any window events, so the rules for
// ending a capture are testable headlessly.
class KeyCapture {
public:
  using Callback = std::function<void(const KeyChord&)>;

  void begin(Callback callback)
  {
    m_callback = std::move(callback);
  }

  void cancel()
  {
    m_callback = nullptr;
  }

  [[nodiscard]] bool active() const
  {
    return static_cast<bool>(m_callback);
  }

  // Clears the slot, then hands the chord to the callback.
  void complete(const KeyChord& chord)
  {
    const auto callback = std::exchange(m_callback, nullptr);
    if (callback)
    {
      callback(chord);
    }
  }

private:
  Callback m_callback;
};

#endif  // KEYCAPTURE_H
