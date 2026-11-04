#ifndef PROFILERSERVERLANE_H
#define PROFILERSERVERLANE_H

#include <History.h>
#include <ProfileStats.h>
#include <ProfilerView.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

// What the Profiler panel draws for the server's tick lane and the Network section. Plain data the editor app fills
// from net::RemoteStats, so the editor library needs no network types.
enum class ServerLaneState : uint8_t { off, waiting, live, unsupported, disconnected };

struct ServerTrafficRow {
  std::string type;
  uint64_t bytes = 0;
  double share = 0.0;
};

struct ServerTrafficDirection {
  double bytesPerSecond = 0.0;
  std::vector<ServerTrafficRow> types;
};

struct ServerSnapshotRow {
  double secondsAgo = 0.0;
  uint32_t bytes = 0;
  std::string reason;
  double buildMillis = 0.0;
  uint16_t recipients = 0;
};

struct ServerLaneInput {
  ServerLaneState state = ServerLaneState::off;
  // Null until something arrived.
  const profile::History* history = nullptr;
  std::vector<profile::ViewNode> nodes;
  bool sceneRunning = false;
  bool debugBuild = false;
  bool localServer = true;
  std::size_t droppedStepsCounter = 0;
  std::size_t ticksCounter = 0;
  // The server's per-tick durations, oldest first; one sample can hold several ticks.
  std::vector<uint32_t> tickMicros;
  ServerTrafficDirection sent;
  ServerTrafficDirection received;
  // Newest first, at most maxSnapshotRows.
  std::vector<ServerSnapshotRow> snapshots;
  uint32_t largestSnapshotBytes = 0;
  std::size_t snapshotsLastMinute = 0;
  uint16_t connections = 0;
  uint16_t editors = 0;
  // The editor's own inbound rate, from its client's counters.
  double editorReceivedBytesPerSecond = 0.0;
};

namespace profilerServerLane {
  struct TickSummary {
    double averageMillis = 0.0;
    double p95Millis = 0.0;
    double maxMillis = 0.0;
    std::size_t ticks = 0;
  };

  // The newest ticks the window's samples hold, according to their ticks counters, capped at what was kept.
  [[nodiscard]] inline std::size_t windowTickCount(const profile::History& history, const std::size_t window,
                                                   const std::size_t ticksCounter, const std::size_t available)
  {
    const int64_t counted = profile::sumCounter(history, window, ticksCounter);

    return counted <= 0 ? 0 : std::min(static_cast<std::size_t>(counted), available);
  }

  [[nodiscard]] inline TickSummary summarizeTicks(const std::vector<uint32_t>& tickMicros, const std::size_t count)
  {
    TickSummary summary;
    const std::size_t used = std::min(count, tickMicros.size());

    if (used == 0)
    {
      return summary;
    }

    std::vector<uint32_t> newest(tickMicros.end() - static_cast<std::ptrdiff_t>(used), tickMicros.end());
    uint64_t total = 0;
    uint32_t largest = 0;

    for (const uint32_t micros : newest)
    {
      total += micros;
      largest = std::max(largest, micros);
    }

    summary.ticks = used;
    summary.averageMillis = static_cast<double>(total) / static_cast<double>(used) / 1000.0;
    summary.maxMillis = largest / 1000.0;
    summary.p95Millis = profile::percentile(std::move(newest), 95.0) / 1000.0;

    return summary;
  }

  inline constexpr std::size_t maxSnapshotRows = 10;

  // The line shown instead of, or above, the lane's chart; empty when there is none.
  [[nodiscard]] inline std::string notice(const ServerLaneInput& input)
  {
    switch (input.state)
    {
      case ServerLaneState::waiting: return "Waiting for server statistics...";
      case ServerLaneState::unsupported: return "This server does not report statistics.";
      case ServerLaneState::disconnected: return "Disconnected";
      case ServerLaneState::live: return input.sceneRunning ? std::string() : std::string("Scene stopped");
      case ServerLaneState::off: break;
    }

    return {};
  }

  // 1024-based with one decimal and no locale, for example "61.2 KB/s". The value is rounded as it will print before
  // the unit is chosen, so 1023.96 KB reads "1.0 MB/s". Negative and NaN rates read as 0.
  [[nodiscard]] inline std::string formatRate(const double bytesPerSecond)
  {
    constexpr const char* units[] = {"B/s", "KB/s", "MB/s", "GB/s"};
    double value = bytesPerSecond > 0.0 ? bytesPerSecond : 0.0;
    std::size_t unit = 0;

    const auto printed = [](const double v, const std::size_t u) {
      return u == 0 ? std::floor(v + 0.5) : std::floor(v * 10.0 + 0.5) / 10.0;
    };

    while (unit + 1 < std::size(units) && printed(value, unit) >= 1024.0)
    {
      value /= 1024.0;
      ++unit;
    }

    if (unit == 0)
    {
      return std::to_string(static_cast<uint64_t>(value + 0.5)) + " " + units[unit];
    }

    const auto tenths = static_cast<uint64_t>(value * 10.0 + 0.5);

    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " " + units[unit];
  }

  // A size such as "12.3 KB", with the same rules as formatRate.
  [[nodiscard]] inline std::string formatSize(const uint64_t bytes)
  {
    std::string text = formatRate(static_cast<double>(bytes));
    text.erase(text.size() - 2);

    return text;
  }

  // "61.2 KB/s  (stateDelta 80%, snapshot 15%)" with the largest `maxTypes` types.
  [[nodiscard]] inline std::string describeTraffic(const ServerTrafficDirection& direction,
                                                   const std::size_t maxTypes = 3)
  {
    std::string text = formatRate(direction.bytesPerSecond);

    if (direction.types.empty() || maxTypes == 0)
    {
      return text;
    }

    text += "  (";

    for (std::size_t i = 0; i < direction.types.size() && i < maxTypes; ++i)
    {
      const auto percent = static_cast<uint64_t>(direction.types[i].share * 100.0 + 0.5);
      text += (i == 0 ? "" : ", ") + direction.types[i].type + " " + std::to_string(percent) + "%";
    }

    return text + ")";
  }

  // "last 12.3 KB (join, 4s ago)  largest 90.0 KB  in last minute: 3"
  [[nodiscard]] inline std::string describeSnapshots(const ServerLaneInput& input)
  {
    const std::string lastMinute = "in last minute: " + std::to_string(input.snapshotsLastMinute);

    if (input.snapshots.empty())
    {
      return "none yet  " + lastMinute;
    }

    const auto& last = input.snapshots.front();

    return "last " + formatSize(last.bytes) + " (" + last.reason + ", " +
           std::to_string(static_cast<uint64_t>(last.secondsAgo)) + "s ago)  largest " +
           formatSize(input.largestSnapshotBytes) + "  " + lastMinute;
  }

  // The editor's inbound byte rate, from a monotonic total read once per frame. The rate is recomputed once per
  // minInterval so a frame-to-frame jitter in arrivals does not make the figure flicker.
  class RateMeter {
  public:
    using Clock = std::chrono::steady_clock;

    static constexpr auto minInterval = std::chrono::milliseconds(500);

    // Forgets the history, so a later reading does not average over time nobody was measuring.
    void reset()
    {
      *this = RateMeter();
    }

    // 0 until the first interval has passed, and when the total went back.
    double update(const uint64_t total, const Clock::time_point now)
    {
      if (!m_primed || total < m_total || now < m_time)
      {
        m_primed = true;
        m_rate = 0.0;
        m_total = total;
        m_time = now;
      }
      else if (now - m_time >= minInterval)
      {
        m_rate = static_cast<double>(total - m_total) / std::chrono::duration<double>(now - m_time).count();
        m_total = total;
        m_time = now;
      }

      return m_rate;
    }

  private:
    bool m_primed = false;
    uint64_t m_total = 0;
    double m_rate = 0.0;
    Clock::time_point m_time{};
  };
}

[[nodiscard]] inline std::string serverLaneNotice(const ServerLaneInput& input)
{
  return profilerServerLane::notice(input);
}

#endif
