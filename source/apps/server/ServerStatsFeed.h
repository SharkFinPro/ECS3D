#ifndef SERVERSTATSFEED_H
#define SERVERSTATSFEED_H

#include "StatsSummary.h"
#include <Lane.h>
#include <ProfileScope.h>
#include <ServerStats.h>
#include <SnapshotLog.h>
#include <TrafficCounters.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

// What the server hands editors in a serverStats message, gathered from the lane, the traffic totals and the snapshot
// log. Free of ServerApp so the collection is pinned without a server.

static_assert(net::maxServerStatsNodes == profile::maxNodes);

// What the previous serverStats already covered.
struct StatsFeedCursor
{
  uint64_t sequence = 0;
  uint64_t tickPushed = 0;
  net::TrafficTotals out;
  net::TrafficTotals in;
  uint64_t snapshots = 0;
};

// The plain values the feed reads besides the lane.
struct StatsFeedInput
{
  NetStatsSample net;
  int64_t editors = 0;
  // debugBuild, editMode and sceneRunning; the feed never sets nodeTable.
  uint8_t flags = 0;
  std::chrono::steady_clock::time_point now;
};

namespace statsFeedDetail {
  template <typename T>
  [[nodiscard]] T clampTo(const int64_t value)
  {
    return static_cast<T>(std::clamp<int64_t>(value, 0, std::numeric_limits<T>::max()));
  }

  [[nodiscard]] inline std::size_t freshTicks(const profile::DurationRing& ring, const StatsFeedCursor& cursor)
  {
    return static_cast<std::size_t>(std::min<uint64_t>(ring.totalPushed() - cursor.tickPushed, ring.size()));
  }

  [[nodiscard]] inline std::size_t freshSnapshots(const net::SnapshotLog& log, const StatsFeedCursor& cursor)
  {
    return static_cast<std::size_t>(std::min<uint64_t>(log.totalRecorded() - cursor.snapshots, log.size()));
  }

  inline void fillNodes(const profile::ScopeTree& tree, net::ServerStats& stats)
  {
    const auto count = std::min(tree.size(), profile::maxNodes);
    stats.nodes.reserve(count > 0 ? count - 1 : 0);
    for (std::size_t id = 1; id < count; ++id)
    {
      const auto node = static_cast<profile::NodeId>(id);
      stats.nodes.push_back({tree.parent(node), std::string(profile::nameOf(tree.name(node)))});
    }
  }

  inline void fillSamples(const profile::Lane& lane, const StatsCounters& counters, const StatsFeedCursor& cursor,
                          net::ServerStats& stats)
  {
    const auto& history = lane.history();
    const auto nodeCount = std::min(lane.tree().size(), profile::maxNodes);
    const auto fresh = std::min(samplesSince(history, cursor.sequence), net::maxServerStatsSamples);

    stats.samples.reserve(fresh);
    for (std::size_t i = history.size() - fresh; i < history.size(); ++i)
    {
      const auto& sample = history.at(i);
      net::ServerStatsSample out;
      out.wallMicros = sample.wallMicros;
      out.ticks = clampTo<uint8_t>(sample.counters[counters.ticks]);
      out.droppedSteps = clampTo<uint8_t>(sample.counters[counters.droppedSteps]);

      for (std::size_t id = 1; id < nodeCount; ++id)
      {
        if (sample.nodeMicros[id] != 0 || sample.nodeCalls[id] != 0)
        {
          out.nodes.push_back({static_cast<uint16_t>(id), sample.nodeMicros[id], sample.nodeCalls[id]});
        }
      }

      stats.samples.push_back(std::move(out));
    }
  }

  inline void fillTicks(const profile::Lane& lane, const StatsFeedCursor& cursor, net::ServerStats& stats)
  {
    const auto* ring = lane.tracked();
    if (ring == nullptr)
    {
      return;
    }

    const auto fresh = std::min(freshTicks(*ring, cursor), net::maxServerStatsTicks);
    stats.tickMicros.reserve(fresh);
    for (std::size_t i = ring->size() - fresh; i < ring->size(); ++i)
    {
      stats.tickMicros.push_back(ring->at(i));
    }
  }

  inline void fillTraffic(const net::TrafficTotals& now, const net::TrafficTotals& before,
                          std::vector<net::ServerStatsTraffic>& out)
  {
    for (std::size_t type = 0; type < net::messageTypeCount; ++type)
    {
      const auto messages = now.messages[type] - before.messages[type];
      const auto bytes = now.bytes[type] - before.bytes[type];
      if (messages != 0 || bytes != 0)
      {
        out.push_back({static_cast<uint8_t>(type),
                       static_cast<uint32_t>(std::min<uint64_t>(messages, std::numeric_limits<uint32_t>::max())),
                       bytes});
      }
    }
  }

  inline void fillSnapshots(const net::SnapshotLog* log, const StatsFeedCursor& cursor,
                            const std::chrono::steady_clock::time_point now, net::ServerStats& stats)
  {
    if (log == nullptr)
    {
      return;
    }

    const auto fresh = std::min(freshSnapshots(*log, cursor), net::maxServerStatsSnapshots);
    for (std::size_t i = log->size() - fresh; i < log->size(); ++i)
    {
      const auto& record = log->at(i);
      const auto ageMillis = std::chrono::duration_cast<std::chrono::milliseconds>(now - record.time).count();
      stats.snapshots.push_back({clampTo<uint32_t>(ageMillis), record.bytes, record.reason, record.buildMicros,
                                 record.recipients});
    }
  }

  inline void advance(const profile::Lane& lane, StatsFeedCursor& cursor, const StatsFeedInput& input)
  {
    const auto& history = lane.history();
    if (history.size() > 0)
    {
      cursor.sequence = history.newest().sequence;
    }

    if (const auto* ring = lane.tracked())
    {
      cursor.tickPushed = ring->totalPushed();
    }

    cursor.out = input.net.out;
    cursor.in = input.net.in;
    if (input.net.snapshots != nullptr)
    {
      cursor.snapshots = input.net.snapshots->totalRecorded();
    }
  }
}

// Everything since `cursor`, then advances it. nodeTableEpoch is the tree's node count, which only grows; `nodes`
// always holds the whole table and the caller decides per recipient whether it is packed.
[[nodiscard]] inline net::ServerStats collectServerStats(const profile::Lane& lane, const StatsCounters& counters,
                                                         StatsFeedCursor& cursor, const StatsFeedInput& input)
{
  net::ServerStats stats;
  stats.flags = input.flags;
  stats.nodeTableEpoch = static_cast<uint32_t>(lane.tree().size());
  statsFeedDetail::fillNodes(lane.tree(), stats);
  statsFeedDetail::fillSamples(lane, counters, cursor, stats);
  statsFeedDetail::fillTicks(lane, cursor, stats);
  statsFeedDetail::fillTraffic(input.net.out, cursor.out, stats.out);
  statsFeedDetail::fillTraffic(input.net.in, cursor.in, stats.in);
  statsFeedDetail::fillSnapshots(input.net.snapshots, cursor, input.now, stats);

  stats.connections = statsFeedDetail::clampTo<uint16_t>(input.net.connections);
  stats.editors = statsFeedDetail::clampTo<uint16_t>(input.editors);
  stats.objects = lane.history().size() > 0
    ? statsFeedDetail::clampTo<uint32_t>(lane.history().newest().counters[counters.objects])
    : 0;

  statsFeedDetail::advance(lane, cursor, input);
  return stats;
}

// Advances the cursor to now without building anything, for when nobody is subscribed, so the first message after a
// subscribe does not carry the backlog.
inline void skipServerStats(const profile::Lane& lane, StatsFeedCursor& cursor, const StatsFeedInput& input)
{
  statsFeedDetail::advance(lane, cursor, input);
}

#endif //SERVERSTATSFEED_H
