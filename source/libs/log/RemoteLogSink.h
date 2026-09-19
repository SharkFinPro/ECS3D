#ifndef REMOTELOGSINK_H
#define REMOTELOGSINK_H

#include "LogSink.h"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

// Buffers log entries for a server to forward to its authorized editor connections over the wire.
// Bounded: once `capacity` entries are queued unread, the oldest is dropped and counted rather than the
// queue growing without limit - a log storm (or no editor ever draining it) costs memory and dropped
// history, never an unbounded queue. Thread-safe, and write() only queues - it must never log itself
// (see LogSink.h) or that would be an entry forwarding entries about forwarding entries.
class RemoteLogSink : public LogSink {
public:
  explicit RemoteLogSink(std::size_t capacity = 500);

  void write(const LogEntry& entry) override;

  struct Drained {
    std::vector<LogEntry> entries;
    // Entries evicted by write() (capacity overflow) since the last drain() call, not counted in entries.
    std::uint64_t dropped = 0;
  };

  // Takes up to maxCount queued entries (oldest first), resetting the drop count carried in the result.
  // Bounds how much one call - one server tick, in ServerApp - can hand to the network in one go; anything
  // left queued is picked up by the next drain.
  [[nodiscard]] Drained drain(std::size_t maxCount);

private:
  mutable std::mutex m_mutex;
  std::size_t m_capacity;
  std::deque<LogEntry> m_entries;
  std::uint64_t m_droppedSinceLastDrain = 0;
};

#endif  // REMOTELOGSINK_H
