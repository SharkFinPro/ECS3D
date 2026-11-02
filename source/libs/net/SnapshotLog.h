#ifndef SNAPSHOTLOG_H
#define SNAPSHOTLOG_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace net {

// What made the server send a full snapshot.
enum class SnapshotReason : uint8_t {
  join,
  sceneEdit,
  editRefusedResync,
  sceneControl,
  projectLoad,
  assetChange,
  scriptReload,
  other
};

[[nodiscard]] constexpr std::string_view snapshotReasonName(const SnapshotReason reason) noexcept
{
  switch (reason)
  {
    case SnapshotReason::join: return "join";
    case SnapshotReason::sceneEdit: return "sceneEdit";
    case SnapshotReason::editRefusedResync: return "editRefusedResync";
    case SnapshotReason::sceneControl: return "sceneControl";
    case SnapshotReason::projectLoad: return "projectLoad";
    case SnapshotReason::assetChange: return "assetChange";
    case SnapshotReason::scriptReload: return "scriptReload";
    case SnapshotReason::other: return "other";
  }

  return "unknown";
}

struct SnapshotRecord {
  SnapshotReason reason = SnapshotReason::other;
  // The payload size, without the type byte that the traffic counters add.
  uint32_t bytes = 0;
  uint32_t buildMicros = 0;
  uint16_t recipients = 0;
};

// The most recent snapshots, oldest first. Touched on the tick thread only.
class SnapshotLog {
public:
  static constexpr std::size_t capacity = 32;

  void record(const SnapshotRecord& entry) noexcept
  {
    m_entries[m_totalRecorded % capacity] = entry;
    ++m_totalRecorded;
  }

  // Monotonic, so a reader diffs it against an earlier read to count what is new.
  [[nodiscard]] uint64_t totalRecorded() const noexcept { return m_totalRecorded; }

  [[nodiscard]] std::size_t size() const noexcept
  {
    return m_totalRecorded < capacity ? static_cast<std::size_t>(m_totalRecorded) : capacity;
  }

  // 0 is the oldest entry still held, size() - 1 the newest.
  [[nodiscard]] const SnapshotRecord& at(const std::size_t index) const noexcept
  {
    return m_entries[(m_totalRecorded - size() + index) % capacity];
  }

private:
  std::array<SnapshotRecord, capacity> m_entries{};
  uint64_t m_totalRecorded = 0;
};

}

#endif //SNAPSHOTLOG_H
