#ifndef TRAFFICCOUNTERS_H
#define TRAFFICCOUNTERS_H

#include <Protocol.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace net {

struct TrafficTotals {
  std::array<uint64_t, messageTypeCount> messages{};
  // The payload plus the type byte, as handed to the transport; the transport's own framing is not included.
  std::array<uint64_t, messageTypeCount> bytes{};
  // Outbound messages refused as too large.
  uint64_t refused = 0;
};

// Monotonic per-type message and byte totals. Readers diff two reads to get a rate. Relaxed atomics: the socket
// threads count inbound traffic while the tick thread counts outbound and reads, and a read only needs each
// total to be correct on its own.
class TrafficCounters {
public:
  void countOut(MessageType type, std::size_t payloadBytes, std::size_t recipients) noexcept;

  // Called on socket threads.
  void countIn(MessageType type, std::size_t payloadBytes) noexcept;

  void countRefused() noexcept;

  [[nodiscard]] TrafficTotals totalOut() const noexcept;

  [[nodiscard]] TrafficTotals totalIn() const noexcept;

private:
  struct Side {
    std::array<std::atomic<uint64_t>, messageTypeCount> messages{};
    std::array<std::atomic<uint64_t>, messageTypeCount> bytes{};
  };

  static void add(Side& side, MessageType type, uint64_t messages, uint64_t bytes) noexcept;

  [[nodiscard]] static TrafficTotals read(const Side& side) noexcept;

  Side m_out;
  Side m_in;
  std::atomic<uint64_t> m_refused{0};
};

}

#endif //TRAFFICCOUNTERS_H
