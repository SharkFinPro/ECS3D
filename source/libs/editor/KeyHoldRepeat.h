#ifndef KEYHOLDREPEAT_H
#define KEYHOLDREPEAT_H

#include <Keybinds.h>
#include <chrono>
#include <functional>
#include <optional>
#include <utility>

// First repeat 400 ms after the press, then eight steps a second.
inline constexpr std::chrono::milliseconds keyHoldInitialDelay{400};
inline constexpr std::chrono::milliseconds keyHoldRepeatInterval{125};

// Tracks one held key and reports when its action is due to fire again. Polled once per frame rather
// than driven by the OS key repeat, whose rate varies by machine. The clock is injectable so the timing
// is testable without sleeping.
class KeyHoldRepeat {
public:
  using Clock = std::chrono::steady_clock;
  using NowFunction = std::function<Clock::time_point()>;

  explicit KeyHoldRepeat(const std::chrono::milliseconds initialDelay = keyHoldInitialDelay,
                         const std::chrono::milliseconds interval = keyHoldRepeatInterval,
                         NowFunction now = &Clock::now)
    : m_initialDelay(initialDelay),
      m_interval(interval),
      m_now(std::move(now))
  {}

  void start(const int key, const EditorAction action)
  {
    m_held = Held{ key, action };
    m_nextDue = m_now() + m_initialDelay;
  }

  void release(const int key)
  {
    if (m_held && m_held->key == key)
    {
      stop();
    }
  }

  void stop()
  {
    m_held.reset();
  }

  [[nodiscard]] bool active() const
  {
    return m_held.has_value();
  }

  [[nodiscard]] std::optional<EditorAction> poll()
  {
    if (!m_held)
    {
      return std::nullopt;
    }

    const auto now = m_now();
    if (now < m_nextDue)
    {
      return std::nullopt;
    }

    // Scheduled from now, not from the old due time, so a frame hitch yields one step instead of a burst.
    m_nextDue = now + m_interval;
    return m_held->action;
  }

private:
  struct Held {
    int key;
    EditorAction action;
  };

  std::chrono::milliseconds m_initialDelay;
  std::chrono::milliseconds m_interval;
  NowFunction m_now;
  std::optional<Held> m_held;
  Clock::time_point m_nextDue;
};

#endif  // KEYHOLDREPEAT_H
