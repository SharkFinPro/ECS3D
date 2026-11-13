#ifndef TRANSFORMPUSHGATE_H
#define TRANSFORMPUSHGATE_H

#include <cstdint>
#include <optional>

// Remembers the world update id last pushed to a render object, so the per-frame position, scale and rotation
// push (an Euler to quaternion conversion and a walk up the parents) is skipped while nothing changed. It lives
// with the cached render object, so a replaced render object starts with an empty gate and is always pushed.
struct TransformPushGate {
  // True, and records the id, when nothing was pushed yet or the id differs from the last one pushed.
  [[nodiscard]] bool needsPush(const uint64_t worldUpdateID)
  {
    if (m_pushed && *m_pushed == worldUpdateID)
    {
      return false;
    }

    m_pushed = worldUpdateID;
    return true;
  }

  std::optional<uint64_t> m_pushed;
};

#endif //TRANSFORMPUSHGATE_H
