#ifndef STATSSUMMARY_H
#define STATSSUMMARY_H

#include <Lane.h>
#include <ProfileStats.h>
#include <ProfileScope.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The server's periodic stats line, kept free of ServerApp so its text and arithmetic are pinned without a server.

struct StatsInput
{
  double coveredSeconds = 0.0;
  double tickAvgMs = 0.0;
  double tickP50Ms = 0.0;
  double tickP95Ms = 0.0;
  double tickP99Ms = 0.0;
  double tickMaxMs = 0.0;
  int64_t ticks = 0;
  int64_t droppedSteps = 0;
  double scriptsMs = 0.0;
  double physicsMs = 0.0;
  double collisionsMs = 0.0;
  double replicationMs = 0.0;
  int64_t objects = 0;
};

namespace statsDetail {
  // Integer arithmetic instead of printf or iostreams, so the text never depends on the locale.
  [[nodiscard]] inline std::string fixed(const double value, const int decimals)
  {
    int64_t scale = 1;
    for (int i = 0; i < decimals; ++i)
    {
      scale *= 10;
    }

    const auto scaled = static_cast<int64_t>(std::llround(std::max(value, 0.0) * static_cast<double>(scale)));
    std::string fraction = std::to_string(scaled % scale);
    fraction.insert(0, static_cast<std::size_t>(decimals) - fraction.size(), '0');
    return std::to_string(scaled / scale) + "." + fraction;
  }

  [[nodiscard]] inline std::string ms(const double value)
  {
    return fixed(value, 2);
  }
}

[[nodiscard]] inline std::string formatStatsLine(const StatsInput& in)
{
  using statsDetail::fixed;
  using statsDetail::ms;

  return "stats " + fixed(in.coveredSeconds, 1) + "s: tick avg " + ms(in.tickAvgMs) + " p50 " + ms(in.tickP50Ms) +
         " p95 " + ms(in.tickP95Ms) + " p99 " + ms(in.tickP99Ms) + " max " + ms(in.tickMaxMs) + " ms | ticks " +
         std::to_string(in.ticks) + " | dropped steps " + std::to_string(in.droppedSteps) +
         " | per tick period: scripts " + ms(in.scriptsMs) + " physics " + ms(in.physicsMs) + " collisions " +
         ms(in.collisionsMs) + " replication " + ms(in.replicationMs) + " ms | objects " + std::to_string(in.objects);
}

[[nodiscard]] inline std::string idleStatsLine()
{
  return "stats: idle (no ticks)";
}

struct StatsCounters
{
  uint8_t ticks = 0;
  uint8_t droppedSteps = 0;
  uint8_t objects = 0;
};

// What the previous summary already covered.
struct StatsCursor
{
  uint64_t sequence = 0;
  uint64_t tickPushed = 0;
};

// The node named `name` directly under `parent`, without creating one.
[[nodiscard]] inline std::optional<profile::NodeId> findStatsNode(const profile::ScopeTree& tree,
                                                                  const profile::NodeId parent,
                                                                  const std::string_view name)
{
  for (const auto child : tree.children(parent))
  {
    if (profile::nameOf(tree.name(child)) == name)
    {
      return child;
    }
  }

  return std::nullopt;
}

// Summarizes the samples closed and the "Server tick" durations pushed since `cursor`, then advances it.
[[nodiscard]] inline StatsInput gatherStats(const profile::Lane& lane, const StatsCounters& counters,
                                            StatsCursor& cursor)
{
  StatsInput in;
  const auto& history = lane.history();

  std::size_t window = 0;
  if (history.size() > 0)
  {
    const auto newest = history.newest().sequence;
    window = static_cast<std::size_t>(std::min<uint64_t>(newest - std::min(cursor.sequence, newest), history.size()));
    cursor.sequence = newest;
  }

  if (window > 0)
  {
    const auto& first = history.at(history.size() - window);
    const auto& last = history.newest();
    const auto spanNanos = last.startNanos + static_cast<int64_t>(last.wallMicros) * 1000 - first.startNanos;
    in.coveredSeconds = static_cast<double>(spanNanos) / 1e9;
    in.objects = last.counters[counters.objects];

    for (std::size_t i = history.size() - window; i < history.size(); ++i)
    {
      in.ticks += history.at(i).counters[counters.ticks];
      in.droppedSteps += history.at(i).counters[counters.droppedSteps];
    }
  }

  if (const auto* ring = lane.tracked())
  {
    const auto fresh = static_cast<std::size_t>(std::min<uint64_t>(ring->totalPushed() - cursor.tickPushed, ring->size()));
    cursor.tickPushed = ring->totalPushed();

    auto values = ring->values();
    values.erase(values.begin(), values.end() - static_cast<std::ptrdiff_t>(fresh));
    if (!values.empty())
    {
      uint64_t sum = 0;
      for (const auto micros : values)
      {
        sum += micros;
      }

      in.tickAvgMs = static_cast<double>(sum) / static_cast<double>(values.size()) / 1000.0;
      in.tickP50Ms = profile::percentile(values, 50.0) / 1000.0;
      in.tickP95Ms = profile::percentile(values, 95.0) / 1000.0;
      in.tickP99Ms = profile::percentile(values, 99.0) / 1000.0;
      in.tickMaxMs = *std::max_element(values.begin(), values.end()) / 1000.0;
    }
  }

  const auto averageMs = [&](const std::optional<profile::NodeId> node)
  {
    return node ? profile::summarizeNode(history, *node, window).average / 1000.0 : 0.0;
  };

  const auto& tree = lane.tree();
  const auto serverTick = findStatsNode(tree, 0, "Server tick");
  const auto under = [&](const std::string_view name)
  {
    return serverTick ? findStatsNode(tree, *serverTick, name) : std::nullopt;
  };

  in.scriptsMs = averageMs(under("Scripts"));
  in.physicsMs = averageMs(under("Physics"));
  in.collisionsMs = averageMs(under("Collisions"));
  in.replicationMs = averageMs(findStatsNode(tree, 0, "Replication"));
  return in;
}

#endif //STATSSUMMARY_H
