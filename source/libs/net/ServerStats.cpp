#include "ServerStats.h"
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace net {

namespace {
  // Node ids are below maxServerStatsNodes and id 0 is the root, which has no table entry.
  constexpr std::size_t maxTableNodes = maxServerStatsNodes - 1;

  constexpr std::size_t nodeMinBytes = sizeof(uint16_t) + sizeof(uint32_t);
  constexpr std::size_t nodeTimeBytes = sizeof(uint16_t) + sizeof(uint32_t) + sizeof(uint16_t);
  constexpr std::size_t sampleMinBytes = sizeof(uint32_t) + 2 * sizeof(uint8_t) + sizeof(uint16_t);
  constexpr std::size_t tickBytes = sizeof(uint32_t);
  constexpr std::size_t trafficBytes = sizeof(uint8_t) + sizeof(uint32_t) + sizeof(uint64_t);
  constexpr std::size_t snapshotBytes = 2 * sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint32_t) + sizeof(uint16_t);

  void requireAtMost(const std::size_t count, const std::size_t cap, const char* what)
  {
    if (count > cap)
    {
      throw std::length_error(std::string("serverStats ") + what + " are past their cap");
    }
  }

  void packTraffic(Message& message, const std::vector<ServerStatsTraffic>& entries)
  {
    requireAtMost(entries.size(), std::numeric_limits<uint8_t>::max(), "traffic entries");
    message.write(static_cast<uint8_t>(entries.size()));
    for (const auto& entry : entries)
    {
      message.write(entry.type);
      message.write(entry.messages);
      message.write(entry.bytes);
    }
  }

  // Refuses a count past its cap or past what the remaining bytes could hold, before anything is reserved.
  std::size_t checkedCount(const std::size_t count, const std::size_t cap, const MessageReader& reader,
                           const std::size_t minEntryBytes, const char* what)
  {
    if (count > cap || count > reader.remaining() / minEntryBytes)
    {
      throw std::runtime_error(std::string("serverStats ") + what + " count is past its cap or the payload");
    }

    return count;
  }

  std::vector<ServerStatsNode> unpackNodes(MessageReader& reader)
  {
    const auto count = checkedCount(reader.read<uint16_t>(), maxTableNodes, reader, nodeMinBytes, "node");
    std::vector<ServerStatsNode> nodes;
    nodes.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
      ServerStatsNode node;
      node.parent = reader.read<uint16_t>();
      node.name = reader.readString();
      if (node.parent > i)
      {
        throw std::runtime_error("serverStats node parent is not an earlier node");
      }

      nodes.push_back(std::move(node));
    }

    return nodes;
  }

  ServerStatsSample unpackSample(MessageReader& reader)
  {
    ServerStatsSample sample;
    sample.wallMicros = reader.read<uint32_t>();
    sample.ticks = reader.read<uint8_t>();
    sample.droppedSteps = reader.read<uint8_t>();

    const auto entries = checkedCount(reader.read<uint16_t>(), maxServerStatsNodes, reader, nodeTimeBytes,
                                      "sample entry");
    sample.nodes.reserve(entries);
    for (std::size_t i = 0; i < entries; ++i)
    {
      ServerStatsNodeTime time;
      time.node = reader.read<uint16_t>();
      time.micros = reader.read<uint32_t>();
      time.calls = reader.read<uint16_t>();
      if (time.node >= maxServerStatsNodes)
      {
        throw std::runtime_error("serverStats sample names a node past the node cap");
      }

      sample.nodes.push_back(time);
    }

    return sample;
  }

  std::vector<ServerStatsTraffic> unpackTraffic(MessageReader& reader)
  {
    const auto count = checkedCount(reader.read<uint8_t>(), std::numeric_limits<uint8_t>::max(), reader,
                                    trafficBytes, "traffic");
    std::vector<ServerStatsTraffic> entries;
    entries.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
      ServerStatsTraffic entry;
      entry.type = reader.read<uint8_t>();
      entry.messages = reader.read<uint32_t>();
      entry.bytes = reader.read<uint64_t>();
      entries.push_back(entry);
    }

    return entries;
  }

  ServerStatsSnapshot unpackSnapshot(MessageReader& reader)
  {
    ServerStatsSnapshot snapshot;
    snapshot.ageMillis = reader.read<uint32_t>();
    snapshot.bytes = reader.read<uint32_t>();

    const auto reason = reader.read<uint8_t>();
    if (reason > static_cast<uint8_t>(SnapshotReason::other))
    {
      throw std::runtime_error("serverStats snapshot reason is out of range");
    }

    snapshot.reason = static_cast<SnapshotReason>(reason);
    snapshot.buildMicros = reader.read<uint32_t>();
    snapshot.recipients = reader.read<uint16_t>();
    return snapshot;
  }
}

Message packServerStats(const ServerStats& stats, const bool withNodeTable)
{
  requireAtMost(stats.samples.size(), maxServerStatsSamples, "samples");
  requireAtMost(stats.tickMicros.size(), maxServerStatsTicks, "ticks");
  requireAtMost(stats.snapshots.size(), maxServerStatsSnapshots, "snapshots");
  for (const auto& sample : stats.samples)
  {
    requireAtMost(sample.nodes.size(), maxServerStatsNodes, "sample entries");
  }

  Message message(MessageType::serverStats);
  message.write(serverStatsVersion);

  const auto flags = static_cast<uint8_t>(withNodeTable ? (stats.flags | serverStatsFlags::nodeTable)
                                                        : (stats.flags & ~serverStatsFlags::nodeTable));
  message.write(flags);
  message.write(stats.nodeTableEpoch);

  if (withNodeTable)
  {
    requireAtMost(stats.nodes.size(), maxTableNodes, "nodes");
    message.write(static_cast<uint16_t>(stats.nodes.size()));
    for (const auto& node : stats.nodes)
    {
      message.write(node.parent);
      message.writeString(node.name);
    }
  }

  message.write(static_cast<uint16_t>(stats.samples.size()));
  for (const auto& sample : stats.samples)
  {
    message.write(sample.wallMicros);
    message.write(sample.ticks);
    message.write(sample.droppedSteps);
    message.write(static_cast<uint16_t>(sample.nodes.size()));
    for (const auto& time : sample.nodes)
    {
      message.write(time.node);
      message.write(time.micros);
      message.write(time.calls);
    }
  }

  message.write(static_cast<uint16_t>(stats.tickMicros.size()));
  for (const auto micros : stats.tickMicros)
  {
    message.write(micros);
  }

  packTraffic(message, stats.out);
  packTraffic(message, stats.in);

  message.write(static_cast<uint8_t>(stats.snapshots.size()));
  for (const auto& snapshot : stats.snapshots)
  {
    message.write(snapshot.ageMillis);
    message.write(snapshot.bytes);
    message.write(static_cast<uint8_t>(snapshot.reason));
    message.write(snapshot.buildMicros);
    message.write(snapshot.recipients);
  }

  message.write(stats.connections);
  message.write(stats.editors);
  message.write(stats.objects);
  return message;
}

ServerStats unpackServerStats(const Message& message)
{
  MessageReader reader(message);

  if (reader.read<uint16_t>() != serverStatsVersion)
  {
    throw std::runtime_error("serverStats version is not supported");
  }

  ServerStats stats;
  stats.flags = reader.read<uint8_t>();
  stats.nodeTableEpoch = reader.read<uint32_t>();
  stats.hasNodeTable = (stats.flags & serverStatsFlags::nodeTable) != 0;

  if (stats.hasNodeTable)
  {
    stats.nodes = unpackNodes(reader);
  }

  const auto sampleCount =
    checkedCount(reader.read<uint16_t>(), maxServerStatsSamples, reader, sampleMinBytes, "sample");
  stats.samples.reserve(sampleCount);
  for (std::size_t i = 0; i < sampleCount; ++i)
  {
    stats.samples.push_back(unpackSample(reader));
  }

  const auto tickCount = checkedCount(reader.read<uint16_t>(), maxServerStatsTicks, reader, tickBytes, "tick");
  stats.tickMicros.reserve(tickCount);
  for (std::size_t i = 0; i < tickCount; ++i)
  {
    stats.tickMicros.push_back(reader.read<uint32_t>());
  }

  stats.out = unpackTraffic(reader);
  stats.in = unpackTraffic(reader);

  const auto snapshotCount =
    checkedCount(reader.read<uint8_t>(), maxServerStatsSnapshots, reader, snapshotBytes, "snapshot");
  stats.snapshots.reserve(snapshotCount);
  for (std::size_t i = 0; i < snapshotCount; ++i)
  {
    stats.snapshots.push_back(unpackSnapshot(reader));
  }

  stats.connections = reader.read<uint16_t>();
  stats.editors = reader.read<uint16_t>();
  stats.objects = reader.read<uint32_t>();
  return stats;
}

Message packStatsSubscribe(const bool subscribe)
{
  Message message(MessageType::statsSubscribe);
  message.write(subscribe);
  return message;
}

bool unpackStatsSubscribe(const Message& message)
{
  MessageReader reader(message);
  return reader.read<bool>();
}

}
