#include "RemoteStats.h"
#include <Log.h>
#include <utility>

namespace net {

RemoteStats::RemoteStats(NowFunction now)
  : m_now(std::move(now)),
    m_history(historyCapacity),
    m_ticks(maxServerStatsTicks)
{
  reset();
}

void RemoteStats::reset()
{
  m_history = profile::History(historyCapacity);
  m_ticks = profile::DurationRing(maxServerStatsTicks);
  m_nextSequence = 1;
  m_nextStartNanos = 0;
  m_parents.assign(1, 0);
  m_names.assign(1, std::string());
  m_epoch = 0;
  m_batches.clear();
  m_snapshots.clear();
  m_flags = 0;
  m_connections = 0;
  m_editors = 0;
  m_objects = 0;
  m_unknownNodeEntries = 0;
  m_versionWarned = false;
}

void RemoteStats::subscribed()
{
  reset();
  m_state = State::waiting;
  m_subscribedAt = m_now();
}

void RemoteStats::unsubscribed()
{
  m_state = State::idle;
}

void RemoteStats::disconnected()
{
  m_state = State::disconnected;
}

void RemoteStats::apply(const Message& message)
{
  if (m_state == State::idle || m_state == State::disconnected)
  {
    return;
  }

  MessageReader reader(message);
  if (reader.read<uint16_t>() != serverStatsVersion)
  {
    if (!m_versionWarned)
    {
      m_versionWarned = true;
      Log::warn(LogCategory::net, "The server sends a statistics version this editor does not understand.");
    }

    m_state = State::unsupported;
    return;
  }

  apply(unpackServerStats(message));
}

void RemoteStats::apply(const ServerStats& stats)
{
  if (m_state == State::idle || m_state == State::disconnected)
  {
    return;
  }

  const auto arrival = m_now();
  m_state = State::live;

  if (stats.hasNodeTable)
  {
    m_parents.assign(1, 0);
    m_names.assign(1, std::string());
    for (const auto& node : stats.nodes)
    {
      m_parents.push_back(node.parent);
      m_names.push_back(node.name);
    }
    m_epoch = stats.nodeTableEpoch;
  }

  TrafficBatch batch;
  batch.arrival = arrival;
  batch.out = stats.out;
  batch.in = stats.in;

  for (const auto& source : stats.samples)
  {
    profile::Sample sample;
    sample.sequence = m_nextSequence++;
    sample.startNanos = m_nextStartNanos;
    sample.wallMicros = source.wallMicros;
    sample.counters[ticksCounter] = source.ticks;
    sample.counters[droppedStepsCounter] = source.droppedSteps;

    for (const auto& entry : source.nodes)
    {
      if (entry.node >= m_parents.size())
      {
        ++m_unknownNodeEntries;
        continue;
      }

      sample.nodeMicros[entry.node] = entry.micros;
      sample.nodeCalls[entry.node] = entry.calls;
    }

    m_nextStartNanos += static_cast<int64_t>(source.wallMicros) * 1000;
    batch.wallMicros += source.wallMicros;
    m_history.push(sample);
  }

  for (const auto micros : stats.tickMicros)
  {
    m_ticks.push(micros);
  }

  m_batches.push_back(std::move(batch));
  while (m_batches.size() > trafficBatchCapacity)
  {
    m_batches.pop_front();
  }

  for (const auto& record : stats.snapshots)
  {
    m_snapshots.push_back({record, arrival - std::chrono::milliseconds(record.ageMillis)});
  }
  while (m_snapshots.size() > maxServerStatsSnapshots)
  {
    m_snapshots.pop_front();
  }

  m_flags = stats.flags;
  m_connections = stats.connections;
  m_editors = stats.editors;
  m_objects = stats.objects;
}

RemoteStats::State RemoteStats::state() const
{
  if (m_state == State::waiting && m_now() - m_subscribedAt >= unsupportedAfter)
  {
    return State::unsupported;
  }

  return m_state;
}

const profile::History& RemoteStats::history() const
{
  return m_history;
}

const profile::DurationRing& RemoteStats::tickDurations() const
{
  return m_ticks;
}

std::size_t RemoteStats::nodeCount() const
{
  return m_parents.size();
}

uint16_t RemoteStats::nodeParent(const uint16_t node) const
{
  return m_parents.at(node);
}

const std::string& RemoteStats::nodeName(const uint16_t node) const
{
  return m_names.at(node);
}

uint32_t RemoteStats::nodeTableEpoch() const
{
  return m_epoch;
}

const std::deque<RemoteStats::TrafficBatch>& RemoteStats::trafficBatches() const
{
  return m_batches;
}

const std::deque<RemoteStats::SnapshotEntry>& RemoteStats::snapshots() const
{
  return m_snapshots;
}

uint8_t RemoteStats::flags() const
{
  return m_flags;
}

uint16_t RemoteStats::connections() const
{
  return m_connections;
}

uint16_t RemoteStats::editors() const
{
  return m_editors;
}

uint32_t RemoteStats::objects() const
{
  return m_objects;
}

uint64_t RemoteStats::unknownNodeEntries() const
{
  return m_unknownNodeEntries;
}

}
