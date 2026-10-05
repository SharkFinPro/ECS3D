#ifndef UNDOREQUESTGATE_H
#define UNDOREQUESTGATE_H

#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <utility>

// Whether a request actually sent something, read from the OPPOSITE stack's depth (redo's for undo(),
// undo's for redo()) rather than the one undo()/redo() popped from. A refusal clears the popped-from
// stack down to 0 exactly the way a legitimate pop shrinks it by one, so that stack alone cannot tell a
// refusal from a send. The opposite stack is untouched by a refusal and only ever gains exactly one entry
// on success: both push straight onto it, bypassing record() - the only place maxDepth trimming happens.
[[nodiscard]] inline bool gainedOneEntry(const std::size_t before, const std::size_t after)
{
  return after == before + 1;
}

// The condition shared by the Edit menu's undo and redo items: something to act on, an editable server,
// and no request already in flight.
[[nodiscard]] inline bool canActOnHistoryItem(const std::optional<std::string>& label, const bool serverEditable,
                                              const bool requestInFlight)
{
  return label.has_value() && serverEditable && !requestInFlight;
}

// While pending, a further undo/redo request is ignored until clear() or the timeout passes. Undo
// validates against the editor's replicated view, which only updates on the server's rebroadcast, so a
// second press inside one round trip would validate against a still-stale value. The clock is injectable
// so the timeout is testable without sleeping.
class UndoRequestGate {
public:
  using Clock = std::chrono::steady_clock;
  using NowFunction = std::function<Clock::time_point()>;

  explicit UndoRequestGate(const std::chrono::milliseconds timeout, NowFunction now = &Clock::now)
    : m_timeout(timeout),
      m_now(std::move(now))
  {}

  // Clears the gate itself as a side effect once the timeout has elapsed, so a caller need not poll it
  // separately.
  [[nodiscard]] bool blocked()
  {
    if (!m_pending)
    {
      return false;
    }

    if (m_now() - m_pendingSince >= m_timeout)
    {
      m_pending = false;
      return false;
    }

    return true;
  }

  void begin()
  {
    m_pending = true;
    m_pendingSince = m_now();
  }

  void clear()
  {
    m_pending = false;
  }

private:
  std::chrono::milliseconds m_timeout;
  NowFunction m_now;
  bool m_pending = false;
  Clock::time_point m_pendingSince;
};

#endif //UNDOREQUESTGATE_H
