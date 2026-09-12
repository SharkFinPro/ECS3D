#ifndef RINGBUFFERSINK_H
#define RINGBUFFERSINK_H

#include "LogSink.h"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

// Keeps the most recent `capacity` entries in memory, for a future editor console panel. Thread-safe.
class RingBufferSink : public LogSink {
public:
  explicit RingBufferSink(std::size_t capacity = 2000);

  void write(const LogEntry& entry) override;

  // A copy of the currently retained entries, oldest first.
  [[nodiscard]] std::vector<LogEntry> snapshot() const;

  // Total entries ever written, including ones since evicted - lets a UI cheaply notice new activity
  // without diffing the whole snapshot.
  [[nodiscard]] std::uint64_t sequence() const;

private:
  mutable std::mutex m_mutex;
  std::size_t m_capacity;
  std::deque<LogEntry> m_entries;
  std::uint64_t m_sequence = 0;
};

#endif  // RINGBUFFERSINK_H
