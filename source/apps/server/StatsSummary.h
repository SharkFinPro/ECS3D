#ifndef STATSSUMMARY_H
#define STATSSUMMARY_H

#include <Lane.h>
#include <ProfileStats.h>
#include <ProfileScope.h>
#include <Protocol.h>
#include <SnapshotLog.h>
#include <TrafficCounters.h>

#include <algorithm>
#include <array>
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

  struct TypeBytes
  {
    net::MessageType type = net::MessageType::undefined;
    uint64_t bytes = 0;
  };

  // Traffic since the previous summary; topSent holds the two largest outbound types, empty entries last.
  uint64_t sentBytes = 0;
  uint64_t recvBytes = 0;
  std::array<TypeBytes, 2> topSent{};
  int64_t connections = 0;
  int64_t snapshots = 0;
  net::SnapshotRecord largestSnapshot;
};

// The network figures a summary reads, passed in as plain values so gatherStats needs no server.
struct NetStatsSample
{
  net::TrafficTotals out;
  net::TrafficTotals in;
  int64_t connections = 0;
  const net::SnapshotLog* snapshots = nullptr;
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

  // 1024-based, one truncated decimal, so a size just under a unit never rounds up to the next one.
  [[nodiscard]] inline std::string byteSize(const uint64_t bytes)
  {
    const auto withUnit = [](const uint64_t tenths, const char* unit)
    {
      return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " " + unit;
    };

    if (bytes < 1024)
    {
      return std::to_string(bytes) + " B";
    }

    if (bytes < 1024 * 1024)
    {
      return withUnit(bytes * 10 / 1024, "KB");
    }

    return withUnit(bytes * 10 / (1024 * 1024), "MB");
  }

  [[nodiscard]] inline std::string sentText(const StatsInput& in)
  {
    std::string text = "sent " + byteSize(in.sentBytes);
    std::string types;
    for (const auto& entry : in.topSent)
    {
      if (entry.bytes > 0)
      {
        types += (types.empty() ? "" : ", ") + std::string(net::messageTypeName(entry.type)) + " " + byteSize(entry.bytes);
      }
    }

    return types.empty() ? text : text + " (" + types + ")";
  }

  [[nodiscard]] inline std::string snapshotText(const StatsInput& in)
  {
    std::string text = "snapshots " + std::to_string(in.snapshots);
    if (in.snapshots > 0)
    {
      text += " (largest " + byteSize(in.largestSnapshot.bytes) + ", " +
              std::string(net::snapshotReasonName(in.largestSnapshot.reason)) + ")";
    }

    return text;
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
         ms(in.collisionsMicros) + " replication " + ms(in.replicationMicros) + " ms | objects " + std::to_string(in.objects) +
         " | " + statsDetail::sentText(in) + " recv " + statsDetail::byteSize(in.recvBytes) + " | conns " +
         std::to_string(in.connections) + " | " + statsDetail::snapshotText(in);
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
  net::TrafficTotals reportedOut;
  net::TrafficTotals reportedIn;
  uint64_t reportedSnapshots = 0;
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

  [[nodiscard]] inline uint64_t totalBytes(const net::TrafficTotals& totals)
  {
    uint64_t sum = 0;
    for (const auto bytes : totals.bytes)
    {
      sum += bytes;
    }

    return sum;
  }

  inline void fillTrafficFigures(const NetStatsSample& net, StatsCursor& cursor, StatsInput& in)
  {
    in.sentBytes = totalBytes(net.out) - totalBytes(cursor.reportedOut);
    in.recvBytes = totalBytes(net.in) - totalBytes(cursor.reportedIn);
    in.connections = net.connections;

    for (std::size_t i = 0; i < net::messageTypeCount; ++i)
    {
      const StatsInput::TypeBytes entry{static_cast<net::MessageType>(i), net.out.bytes[i] - cursor.reportedOut.bytes[i]};
      if (entry.bytes > in.topSent[0].bytes)
      {
        in.topSent[1] = in.topSent[0];
        in.topSent[0] = entry;
      }
      else if (entry.bytes > in.topSent[1].bytes)
      {
        in.topSent[1] = entry;
      }
    }

    cursor.reportedOut = net.out;
    cursor.reportedIn = net.in;
  }

  inline void fillSnapshotFigures(const net::SnapshotLog& log, StatsCursor& cursor, StatsInput& in)
  {
    in.snapshots = static_cast<int64_t>(log.totalRecorded() - cursor.reportedSnapshots);
    const auto fresh = static_cast<std::size_t>(std::min<uint64_t>(log.totalRecorded() - cursor.reportedSnapshots, log.size()));
    cursor.reportedSnapshots = log.totalRecorded();

    for (std::size_t i = log.size() - fresh; i < log.size(); ++i)
    {
      if (log.at(i).bytes > in.largestSnapshot.bytes || i == log.size() - fresh)
      {
        in.largestSnapshot = log.at(i);
      }
    }
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
                                            StatsCursor& cursor, const NetStatsSample& net = {})
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
  statsDetail::fillTrafficFigures(net, cursor, in);
  if (net.snapshots != nullptr)
  {
    statsDetail::fillSnapshotFigures(*net.snapshots, cursor, in);
  }

  return in;
}

#endif //STATSSUMMARY_H
