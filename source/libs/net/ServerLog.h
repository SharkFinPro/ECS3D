#ifndef SERVERLOG_H
#define SERVERLOG_H

#include <Protocol.h>
#include <LogEntry.h>
#include <cstdint>
#include <vector>

// Wire (de)serialization for MessageType::serverLog: a batch of the server's own log entries (from
// RemoteLogSink), sent to editor connections only. Depends only on ECS3DLog + ECS3DNetProtocol, so it
// carries no ManagedHost/CLR dependency and can be compiled into a headless test binary directly, the
// same way TransportLog.cpp is.
namespace net {

// Batched (not one message per entry) so a burst of log lines costs one socket send instead of one per
// line - see RemoteLogSink::drain, which is what fills entries/dropped here. The entry's own timestamp
// travels too (as milliseconds since the epoch): the receiving editor writes straight into its
// RingBufferSink rather than through Log::write, which would otherwise stamp every forwarded entry with
// its arrival time instead of when the server actually logged it.
[[nodiscard]] Message packServerLog(const std::vector<LogEntry>& entries, std::uint64_t dropped);

struct ServerLogBatch {
  std::vector<LogEntry> entries;
  // How many entries RemoteLogSink evicted (capacity overflow) before this batch was drained. Carried so
  // the receiving editor can say "N entries were dropped" instead of silently missing history.
  std::uint64_t dropped = 0;
};

[[nodiscard]] ServerLogBatch unpackServerLog(const Message& message);

}

#endif  // SERVERLOG_H
