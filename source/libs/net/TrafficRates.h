#ifndef TRAFFICRATES_H
#define TRAFFICRATES_H

#include "TrafficCounters.h"

#include <Protocol.h>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace net {

struct TrafficRateRow {
  MessageType type = MessageType::undefined;
  std::string_view name;
  double bytesPerSecond = 0.0;
};

// Bytes per second from a monotonic total, recomputed at most once per minInterval so the figures do not flicker.
// A rate is the average since the previous recompute, so the caller updates every frame: a meter updated only now and
// then reports the whole gap between its updates.
class TrafficRateMeter {
public:
  using Clock = std::chrono::steady_clock;

  static constexpr auto minInterval = std::chrono::milliseconds(500);

  // The rates stay 0 until the first interval has passed, and restart when any total went back.
  void update(const TrafficTotals& totals, const Clock::time_point now)
  {
    if (!m_primed || now < m_time || wentBack(totals))
    {
      m_primed = true;
      m_total = 0.0;
      m_rows.clear();
      m_last = totals;
      m_time = now;

      return;
    }

    if (now - m_time < minInterval)
    {
      return;
    }

    const double seconds = std::chrono::duration<double>(now - m_time).count();
    m_rows.clear();
    uint64_t total = 0;

    for (std::size_t i = 0; i < messageTypeCount; ++i)
    {
      const uint64_t bytes = totals.bytes[i] - m_last.bytes[i];

      if (bytes > 0)
      {
        const auto type = static_cast<MessageType>(i);
        m_rows.push_back({ type, messageTypeName(type), static_cast<double>(bytes) / seconds });
        total += bytes;
      }
    }

    std::stable_sort(m_rows.begin(), m_rows.end(), [](const TrafficRateRow& a, const TrafficRateRow& b) {
      return a.bytesPerSecond > b.bytesPerSecond;
    });

    m_total = static_cast<double>(total) / seconds;
    m_last = totals;
    m_time = now;
  }

  [[nodiscard]] double totalBytesPerSecond() const { return m_total; }

  // Types that moved bytes in the last interval, largest first.
  [[nodiscard]] const std::vector<TrafficRateRow>& rows() const { return m_rows; }

private:
  [[nodiscard]] bool wentBack(const TrafficTotals& totals) const
  {
    for (std::size_t i = 0; i < messageTypeCount; ++i)
    {
      if (totals.bytes[i] < m_last.bytes[i])
      {
        return true;
      }
    }

    return false;
  }

  bool m_primed = false;
  TrafficTotals m_last;
  double m_total = 0.0;
  std::vector<TrafficRateRow> m_rows;
  Clock::time_point m_time{};
};

}

#endif //TRAFFICRATES_H
