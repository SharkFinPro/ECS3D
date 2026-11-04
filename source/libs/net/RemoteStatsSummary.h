#ifndef REMOTESTATSSUMMARY_H
#define REMOTESTATSSUMMARY_H

#include "RemoteStats.h"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace net {

struct TrafficTypeSummary {
  uint8_t type = 0;
  uint64_t messages = 0;
  uint64_t bytes = 0;
};

struct TrafficDirectionSummary {
  uint64_t bytes = 0;
  uint64_t messages = 0;
  // The server time the summed batches cover.
  double coveredSeconds = 0.0;
  double bytesPerSecond = 0.0;
  // Largest first; equal byte counts keep ascending type order.
  std::vector<TrafficTypeSummary> types;
};

struct TrafficSummary {
  TrafficDirectionSummary sent;
  TrafficDirectionSummary received;
};

namespace summaryDetail {
  inline void add(std::vector<TrafficTypeSummary>& totals, const std::vector<ServerStatsTraffic>& source)
  {
    for (const auto& entry : source)
    {
      auto found = std::ranges::find(totals, entry.type, &TrafficTypeSummary::type);

      if (found == totals.end())
      {
        totals.push_back({entry.type, 0, 0});
        found = totals.end() - 1;
      }

      found->messages += entry.messages;
      found->bytes += entry.bytes;
    }
  }

  inline void finish(TrafficDirectionSummary& direction, const uint64_t coveredMicros)
  {
    std::ranges::sort(direction.types, [](const TrafficTypeSummary& a, const TrafficTypeSummary& b) {
      return a.bytes != b.bytes ? a.bytes > b.bytes : a.type < b.type;
    });

    for (const auto& entry : direction.types)
    {
      direction.bytes += entry.bytes;
      direction.messages += entry.messages;
    }

    direction.coveredSeconds = static_cast<double>(coveredMicros) / 1e6;
    direction.bytesPerSecond =
      coveredMicros == 0 ? 0.0 : static_cast<double>(direction.bytes) / direction.coveredSeconds;
  }
}

// Sums the traffic batches that arrived within `window` of the newest one.
[[nodiscard]] inline TrafficSummary summarizeTraffic(const RemoteStats& stats, const RemoteStats::Clock::duration window)
{
  TrafficSummary summary;
  const auto& batches = stats.trafficBatches();

  if (batches.empty())
  {
    return summary;
  }

  const auto oldestAllowed = batches.back().arrival - window;
  uint64_t coveredMicros = 0;

  for (const auto& batch : batches)
  {
    if (batch.arrival < oldestAllowed)
    {
      continue;
    }

    summaryDetail::add(summary.sent.types, batch.out);
    summaryDetail::add(summary.received.types, batch.in);
    coveredMicros += batch.wallMicros;
  }

  summaryDetail::finish(summary.sent, coveredMicros);
  summaryDetail::finish(summary.received, coveredMicros);

  return summary;
}

// How many held snapshots were taken within `window` before `now`.
[[nodiscard]] inline std::size_t snapshotsWithin(const RemoteStats& stats, const RemoteStats::Clock::time_point now,
                                                 const RemoteStats::Clock::duration window)
{
  return static_cast<std::size_t>(std::ranges::count_if(stats.snapshots(), [&](const auto& entry) {
    return entry.takenAt >= now - window && entry.takenAt <= now;
  }));
}

}

#endif  // REMOTESTATSSUMMARY_H
