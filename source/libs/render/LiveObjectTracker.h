#ifndef LIVEOBJECTTRACKER_H
#define LIVEOBJECTTRACKER_H

#include <uuid.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_set>
#include <vector>

// Collects the uuids drawn each frame and reports whether the set differs from the last committed one, so
// callers prune per-uuid caches only when something was added or removed. The comparison is an
// order-independent signature; a collision only delays pruning until the next change and never drops a live entry.
class LiveObjectTracker {
public:
  void beginFrame()
  {
    m_frameUUIDs.clear();
    m_frameCount = 0;
    m_frameSum = 0;
    m_frameXor = 0;
  }

  void add(const uuids::uuid& uuid)
  {
    m_frameUUIDs.push_back(uuid);

    const auto mixed = mix(uuid);
    ++m_frameCount;
    m_frameSum += mixed;
    m_frameXor ^= mixed;
  }

  // True when this frame's set differs from the last committed one (always on the first frame).
  [[nodiscard]] bool endFrame()
  {
    if (m_committed && m_frameCount == m_count && m_frameSum == m_sum && m_frameXor == m_xor)
    {
      return false;
    }

    m_liveUUIDs.clear();
    m_liveUUIDs.insert(m_frameUUIDs.begin(), m_frameUUIDs.end());

    m_committed = true;
    m_count = m_frameCount;
    m_sum = m_frameSum;
    m_xor = m_frameXor;

    return true;
  }

  [[nodiscard]] const std::unordered_set<uuids::uuid>& liveUUIDs() const
  {
    return m_liveUUIDs;
  }

private:
  static std::uint64_t splitmix(std::uint64_t value)
  {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
  }

  static std::uint64_t mix(const uuids::uuid& uuid)
  {
    std::uint64_t halves[2];
    std::memcpy(halves, uuid.as_bytes().data(), sizeof(halves));

    return splitmix(halves[0]) ^ splitmix(halves[1] + 0x632be59bd9b4e019ULL);
  }

  std::vector<uuids::uuid> m_frameUUIDs;
  std::size_t m_frameCount = 0;
  std::uint64_t m_frameSum = 0;
  std::uint64_t m_frameXor = 0;

  std::unordered_set<uuids::uuid> m_liveUUIDs;
  bool m_committed = false;
  std::size_t m_count = 0;
  std::uint64_t m_sum = 0;
  std::uint64_t m_xor = 0;
};

#endif //LIVEOBJECTTRACKER_H
