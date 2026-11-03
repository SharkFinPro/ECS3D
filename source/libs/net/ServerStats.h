#ifndef SERVERSTATS_H
#define SERVERSTATS_H

#include "SnapshotLog.h"
#include <Protocol.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Wire (de)serialization for MessageType::serverStats and statsSubscribe. Depends only on ECS3DNetProtocol and SnapshotLog.h, so it compiles into the headless test suite.
namespace net {

inline constexpr uint16_t serverStatsVersion = 1;

// Caps unpack enforces before reserving; they match the server lane (profile::maxNodes nodes, a 512-sample history
// and tick ring) and SnapshotLog::capacity.
inline constexpr std::size_t maxServerStatsNodes = 128;
inline constexpr std::size_t maxServerStatsSamples = 512;
inline constexpr std::size_t maxServerStatsTicks = 512;
inline constexpr std::size_t maxServerStatsSnapshots = SnapshotLog::capacity;

namespace serverStatsFlags {
  inline constexpr uint8_t debugBuild = 1;
  inline constexpr uint8_t editMode = 2;
  inline constexpr uint8_t sceneRunning = 4;
  inline constexpr uint8_t nodeTable = 8;
}

struct ServerStatsNode {
  uint16_t parent = 0;
  std::string name;
};

struct ServerStatsNodeTime {
  uint16_t node = 0;
  uint32_t micros = 0;
  uint16_t calls = 0;
};

struct ServerStatsSample {
  uint32_t wallMicros = 0;
  uint8_t ticks = 0;
  uint8_t droppedSteps = 0;
  std::vector<ServerStatsNodeTime> nodes;
};

struct ServerStatsTraffic {
  uint8_t type = 0;
  uint32_t messages = 0;
  uint64_t bytes = 0;
};

struct ServerStatsSnapshot {
  uint32_t ageMillis = 0;
  uint32_t bytes = 0;
  SnapshotReason reason = SnapshotReason::other;
  uint32_t buildMicros = 0;
  uint16_t recipients = 0;
};

struct ServerStats {
  // serverStatsFlags. pack sets the nodeTable bit exactly when it writes the table, and unpack sets it to match.
  uint8_t flags = 0;
  uint32_t nodeTableEpoch = 0;
  bool hasNodeTable = false;
  // Node ids 1..nodes.size() in id order; id 0 is the root. Meaningful only with hasNodeTable.
  std::vector<ServerStatsNode> nodes;
  std::vector<ServerStatsSample> samples;
  std::vector<uint32_t> tickMicros;
  std::vector<ServerStatsTraffic> out;
  std::vector<ServerStatsTraffic> in;
  std::vector<ServerStatsSnapshot> snapshots;
  uint16_t connections = 0;
  uint16_t editors = 0;
  uint32_t objects = 0;
};

// withNodeTable chooses whether this copy carries the node table; the rest is identical for every recipient.
// Throws std::length_error when a vector is past its cap, which is a server bug and never data from the wire.
[[nodiscard]] Message packServerStats(const ServerStats& stats, bool withNodeTable);

// Throws std::runtime_error on a truncated payload, a version other than serverStatsVersion, a count past its cap or
// past what the remaining bytes could hold, a node id >= maxServerStatsNodes, a node parent >= its own id, or an
// out-of-range snapshot reason.
[[nodiscard]] ServerStats unpackServerStats(const Message& message);

[[nodiscard]] Message packStatsSubscribe(bool subscribe);
[[nodiscard]] bool unpackStatsSubscribe(const Message& message);

}

#endif  // SERVERSTATS_H
