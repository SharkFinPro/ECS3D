#ifndef STATSOVERLAY_H
#define STATSOVERLAY_H

#include <History.h>
#include <ProfileStats.h>
#include <ProfilerView.h>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

// The client's F3 overlay: its mode and the headless figures it draws.
enum class StatsOverlayMode : uint8_t { off, compact, detailed };

[[nodiscard]] inline StatsOverlayMode nextStatsOverlayMode(const StatsOverlayMode mode)
{
  switch (mode)
  {
    case StatsOverlayMode::off: return StatsOverlayMode::compact;
    case StatsOverlayMode::compact: return StatsOverlayMode::detailed;
    case StatsOverlayMode::detailed: break;
  }

  return StatsOverlayMode::off;
}

[[nodiscard]] inline StatsOverlayMode initialStatsOverlayMode(const bool statsFlag)
{
  return statsFlag ? StatsOverlayMode::compact : StatsOverlayMode::off;
}

namespace statsOverlay {
  // The time between consecutive stateDeltas as they are drained. A frame that drained none has a maximum of 0.
  class StateUpdateGap {
  public:
    using Clock = std::chrono::steady_clock;

    // The gap since the previous stateDelta in microseconds, at least 1; 0 for the first one.
    uint32_t onStateDelta(const Clock::time_point now)
    {
      uint32_t gap = 0;

      if (m_previous)
      {
        const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now - *m_previous).count();
        gap = static_cast<uint32_t>(std::clamp<int64_t>(micros, 1, std::numeric_limits<uint32_t>::max()));
      }

      m_previous = now;
      m_frameMax = std::max(m_frameMax, gap);

      return gap;
    }

    // The largest gap since the last call, which it clears.
    uint32_t takeFrameMax()
    {
      return std::exchange(m_frameMax, 0u);
    }

  private:
    std::optional<Clock::time_point> m_previous;
    uint32_t m_frameMax = 0;
  };

  struct IntervalStats {
    double averageMillis = 0.0;
    double p95Millis = 0.0;
    double maxMillis = 0.0;
    std::size_t count = 0;   // samples that held a state update
  };

  // Over the newest `window` samples; a sample whose counter is not positive had no state update and is skipped.
  [[nodiscard]] inline IntervalStats summarizeIntervals(const profile::History& history, const std::size_t window,
                                                        const std::size_t counter)
  {
    IntervalStats stats;

    if (counter >= profile::maxCounters)
    {
      return stats;
    }

    std::vector<uint32_t> values;
    const std::size_t count = std::min(window, history.size());

    for (std::size_t i = history.size() - count; i < history.size(); ++i)
    {
      const int64_t micros = history.at(i).counters[counter];

      if (micros > 0)
      {
        values.push_back(static_cast<uint32_t>(std::min<int64_t>(micros, std::numeric_limits<uint32_t>::max())));
      }
    }

    if (values.empty())
    {
      return stats;
    }

    uint64_t total = 0;
    uint32_t largest = 0;

    for (const uint32_t micros : values)
    {
      total += micros;
      largest = std::max(largest, micros);
    }

    stats.count = values.size();
    stats.averageMillis = static_cast<double>(total) / static_cast<double>(values.size()) / 1000.0;
    stats.maxMillis = largest / 1000.0;
    stats.p95Millis = profile::percentile(std::move(values), 95.0) / 1000.0;

    return stats;
  }

  // The 99th percentile of wall time over the newest `window` samples, which the chart's automatic scale comes from.
  [[nodiscard]] inline uint32_t p99WallMicros(const profile::History& history, const std::size_t window)
  {
    std::vector<uint32_t> walls;
    const std::size_t count = std::min(window, history.size());

    for (std::size_t i = history.size() - count; i < history.size(); ++i)
    {
      walls.push_back(history.at(i).wallMicros);
    }

    return profile::percentile(std::move(walls), 99.0);
  }
}

#endif
