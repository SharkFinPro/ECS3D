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
  double tickAvgMicros = 0.0;
  double tickP50Micros = 0.0;
  double tickP95Micros = 0.0;
  double tickP99Micros = 0.0;
  double tickMaxMicros = 0.0;
  int64_t ticks = 0;
  int64_t droppedSteps = 0;
  double scriptsMicros = 0.0;
  double physicsMicros = 0.0;
  double collisionsMicros = 0.0;
  double replicationMicros = 0.0;
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

  // Milliseconds with two decimals from microseconds, rounded once on the integer hundredths.
  [[nodiscard]] inline std::string ms(const double micros)
  {
    const auto hundredths = static_cast<int64_t>(std::llround(std::max(micros, 0.0) / 10.0));
    std::string fraction = std::to_string(hundredths % 100);
    fraction.insert(0, 2 - fraction.size(), '0');
    return std::to_string(hundredths / 100) + "." + fraction;
  }
}

[[nodiscard]] inline std::string formatStatsLine(const StatsInput& in)
{
  using statsDetail::fixed;
  using statsDetail::ms;

  return "stats " + fixed(in.coveredSeconds, 1) + "s: tick avg " + ms(in.tickAvgMicros) + " p50 " + ms(in.tickP50Micros) +
         " p95 " + ms(in.tickP95Micros) + " p99 " + ms(in.tickP99Micros) + " max " + ms(in.tickMaxMicros) + " ms | ticks " +
         std::to_string(in.ticks) + " | dropped steps " + std::to_string(in.droppedSteps) +
         " | per tick period: scripts " + ms(in.scriptsMicros) + " physics " + ms(in.physicsMicros) + " collisions " +
         ms(in.collisionsMicros) + " replication " + ms(in.replicationMicros) + " ms | objects " + std::to_string(in.objects);
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

// What the previous summary already covered, plus running totals the caller keeps current. The totals are not read
// from the history, which caps how many samples a long interval can see.
struct StatsCursor
{
  uint64_t sequence = 0;
  uint64_t tickPushed = 0;
  uint64_t ticks = 0;
  uint64_t droppedSteps = 0;
  uint64_t reportedTicks = 0;
  uint64_t reportedDroppedSteps = 0;
};

// How many of the newest samples have a sequence above `sequence`.
[[nodiscard]] inline std::size_t samplesSince(const profile::History& history, const uint64_t sequence)
{
  std::size_t count = 0;
  while (count < history.size() && history.at(history.size() - 1 - count).sequence > sequence)
  {
    ++count;
  }

  return count;
}

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

namespace statsDetail {
  inline void fillWindowFigures(const profile::History& history, const std::size_t window,
                                const StatsCounters& counters, StatsCursor& cursor, StatsInput& in)
  {
    const auto& first = history.at(history.size() - window);
    const auto& last = history.newest();
    const auto spanNanos = last.startNanos + static_cast<int64_t>(last.wallMicros) * 1000 - first.startNanos;
    in.coveredSeconds = static_cast<double>(spanNanos) / 1e9;
    in.objects = last.counters[counters.objects];
    cursor.sequence = last.sequence;
  }

  inline void fillTickFigures(const profile::DurationRing& ring, StatsCursor& cursor, StatsInput& in)
  {
    const auto fresh =
      static_cast<std::size_t>(std::min<uint64_t>(ring.totalPushed() - cursor.tickPushed, ring.size()));
    cursor.tickPushed = ring.totalPushed();

    auto values = ring.values();
    values.erase(values.begin(), values.end() - static_cast<std::ptrdiff_t>(fresh));
    if (values.empty())
    {
      return;
    }

    uint64_t sum = 0;
    for (const auto micros : values)
    {
      sum += micros;
    }

    in.tickAvgMicros = static_cast<double>(sum) / static_cast<double>(values.size());
    in.tickP50Micros = profile::percentile(values, 50.0);
    in.tickP95Micros = profile::percentile(values, 95.0);
    in.tickP99Micros = profile::percentile(values, 99.0);
    in.tickMaxMicros = *std::max_element(values.begin(), values.end());
  }

  inline void fillNodeAverages(const profile::Lane& lane, const std::size_t window, StatsInput& in)
  {
    const auto& history = lane.history();
    const auto averageMicros = [&](const std::optional<profile::NodeId> node)
    {
      return node ? profile::summarizeNode(history, *node, window).average : 0.0;
    };

    const auto& tree = lane.tree();
    const auto serverTick = findStatsNode(tree, 0, "Server tick");
    const auto under = [&](const std::string_view name)
    {
      return serverTick ? findStatsNode(tree, *serverTick, name) : std::nullopt;
    };

    in.scriptsMicros = averageMicros(under("Scripts"));
    in.physicsMicros = averageMicros(under("Physics"));
    in.collisionsMicros = averageMicros(under("Collisions"));
    in.replicationMicros = averageMicros(findStatsNode(tree, 0, "Replication"));
  }
}

// Summarizes the samples closed and the "Server tick" durations pushed since `cursor`, then advances it. Tick and
// dropped-step counts are the change in the cursor's running totals.
[[nodiscard]] inline StatsInput gatherStats(const profile::Lane& lane, const StatsCounters& counters,
                                            StatsCursor& cursor)
{
  StatsInput in;

  in.ticks = static_cast<int64_t>(cursor.ticks - cursor.reportedTicks);
  in.droppedSteps = static_cast<int64_t>(cursor.droppedSteps - cursor.reportedDroppedSteps);
  cursor.reportedTicks = cursor.ticks;
  cursor.reportedDroppedSteps = cursor.droppedSteps;

  const auto window = samplesSince(lane.history(), cursor.sequence);
  if (window > 0)
  {
    statsDetail::fillWindowFigures(lane.history(), window, counters, cursor, in);
  }

  if (const auto* ring = lane.tracked())
  {
    statsDetail::fillTickFigures(*ring, cursor, in);
  }

  statsDetail::fillNodeAverages(lane, window, in);
  return in;
}

#endif //STATSSUMMARY_H
