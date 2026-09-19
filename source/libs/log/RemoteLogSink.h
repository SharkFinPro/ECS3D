#ifndef REMOTELOGSINK_H
#define REMOTELOGSINK_H

#include "LogSink.h"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <mutex>
#include <vector>

// Buffers log entries for a server to forward to its authorized editor connections over the wire.
// Bounded: once `capacity` entries are queued unread, the oldest is dropped and counted rather than the
// queue growing without limit - a log storm (or no editor ever draining it) costs memory and dropped
// history, never an unbounded queue. Thread-safe, and write() only queues - it must never log itself
// (see LogSink.h) or that would be an entry forwarding entries about forwarding entries.
class RemoteLogSink : public LogSink {
public:
  // A single log call handing write() a message longer than this is truncated (with a marker appended)
  // before it is queued, so one outsized entry cannot dominate either this sink's memory or a drain's
  // byte budget (see drain's maxBytes) on its own.
  static constexpr std::size_t maxMessageBytes = 16 * 1024;

  explicit RemoteLogSink(std::size_t capacity = 500);

  void write(const LogEntry& entry) override;

  struct Drained {
    std::vector<LogEntry> entries;
    // Entries evicted by write() (capacity overflow) since the last drain() call, not counted in entries.
    std::uint64_t dropped = 0;
  };

  // Takes up to maxCount queued entries (oldest first), stopping earlier if their combined size would
  // otherwise pass maxBytes (a rough estimate, not the exact wire size - see packServerLog), and resets
  // the drop count carried in the result. At least one queued entry is always taken when one is
  // available, even if it alone is close to maxBytes, so a single large entry cannot stall the queue
  // forever. Bounds how much one call - one server tick, in ServerApp - can hand to the network in one
  // go; anything left queued is picked up by the next drain.
  [[nodiscard]] Drained drain(std::size_t maxCount, std::size_t maxBytes = std::numeric_limits<std::size_t>::max());

private:
  mutable std::mutex m_mutex;
  std::size_t m_capacity;
  std::deque<LogEntry> m_entries;
  std::uint64_t m_droppedSinceLastDrain = 0;
};

#endif  // REMOTELOGSINK_H
