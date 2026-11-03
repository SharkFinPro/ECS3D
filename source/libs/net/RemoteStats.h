#ifndef REMOTESTATS_H
#define REMOTESTATS_H

#include "ServerStats.h"
#include <History.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace net {

// The editor's copy of a server's statistics, rebuilt from serverStats messages. Touched on the editor's main thread
// only.
class RemoteStats {
public:
  using Clock = std::chrono::steady_clock;
  using NowFunction = std::function<Clock::time_point()>;

  enum class State : uint8_t {
    idle,          // not subscribed
    waiting,       // subscribed, nothing received yet
    live,          // receiving
    unsupported,   // subscribed for unsupportedAfter with nothing received, or the server sends another version
    disconnected   // the connection dropped; the data is kept for display
  };

  static constexpr auto unsupportedAfter = std::chrono::seconds(2);
  static constexpr std::size_t historyCapacity = 512;
  static constexpr std::size_t ticksCounter = 0;
  static constexpr std::size_t droppedStepsCounter = 1;
  static constexpr std::size_t trafficBatchCapacity = 64;

  explicit RemoteStats(NowFunction now = &Clock::now);

  // Clears everything and starts waiting: a new subscription gets a fresh node table from the server.
  void subscribed();
  void unsubscribed();
  // Does nothing when idle.
  void disconnected();

  // Ignores any type but serverStats. Reads the version first: another version logs one warning per subscription and
  // moves to unsupported without throwing. Otherwise unpacks, letting a malformed message's exception propagate, and
  // applies. Ignored when idle or disconnected.
  void apply(const Message& message);

  // Keeps at most profile::maxNodes nodes (root included) and skips sample entries past the held table or that cap,
  // so it is safe for a ServerStats that did not come from unpack.
  void apply(const ServerStats& stats);

  // Evaluated against the clock on each call.
  [[nodiscard]] State state() const;

  [[nodiscard]] const profile::History& history() const;
  [[nodiscard]] const profile::DurationRing& tickDurations() const;

  // Node 0 is the root (empty name). Valid ids are below nodeCount(); before any table, nodeCount() is 1.
  [[nodiscard]] std::size_t nodeCount() const;
  [[nodiscard]] uint16_t nodeParent(uint16_t node) const;
  [[nodiscard]] const std::string& nodeName(uint16_t node) const;
  [[nodiscard]] uint32_t nodeTableEpoch() const;

  struct TrafficBatch {
    Clock::time_point arrival;
    uint64_t wallMicros = 0;
    std::vector<ServerStatsTraffic> out;
    std::vector<ServerStatsTraffic> in;
  };
  // Oldest first, at most trafficBatchCapacity.
  [[nodiscard]] const std::deque<TrafficBatch>& trafficBatches() const;

  struct SnapshotEntry {
    ServerStatsSnapshot record;
    Clock::time_point takenAt;
  };
  // Oldest first, at most maxServerStatsSnapshots.
  [[nodiscard]] const std::deque<SnapshotEntry>& snapshots() const;

  [[nodiscard]] uint8_t flags() const;
  [[nodiscard]] uint16_t connections() const;
  [[nodiscard]] uint16_t editors() const;
  [[nodiscard]] uint32_t objects() const;

  // Sample entries naming a node the held table does not have; such entries are skipped.
  [[nodiscard]] uint64_t unknownNodeEntries() const;

private:
  void reset();

  NowFunction m_now;
  State m_state = State::idle;
  Clock::time_point m_subscribedAt;
  bool m_versionWarned = false;

  profile::History m_history;
  profile::DurationRing m_ticks;
  uint64_t m_nextSequence = 1;
  int64_t m_nextStartNanos = 0;

  // Index 0 is the root.
  std::vector<uint16_t> m_parents;
  std::vector<std::string> m_names;
  uint32_t m_epoch = 0;

  std::deque<TrafficBatch> m_batches;
  std::deque<SnapshotEntry> m_snapshots;

  uint8_t m_flags = 0;
  uint16_t m_connections = 0;
  uint16_t m_editors = 0;
  uint32_t m_objects = 0;
  uint64_t m_unknownNodeEntries = 0;
};

}

#endif  // REMOTESTATS_H
