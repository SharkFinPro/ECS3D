#include "TrafficCounters.h"

namespace net {

namespace {
  // A type outside the table (a corrupt or newer peer's byte) is counted as undefined rather than dropped.
  std::size_t bucketOf(const MessageType type) noexcept
  {
    const auto index = static_cast<std::size_t>(type);
    return index < messageTypeCount ? index : 0;
  }
}

void TrafficCounters::add(Side& side, const MessageType type, const uint64_t messages, const uint64_t bytes) noexcept
{
  const auto bucket = bucketOf(type);
  side.messages[bucket].fetch_add(messages, std::memory_order_relaxed);
  side.bytes[bucket].fetch_add(bytes, std::memory_order_relaxed);
}

TrafficTotals TrafficCounters::read(const Side& side) noexcept
{
  TrafficTotals totals;
  for (std::size_t i = 0; i < messageTypeCount; ++i)
  {
    totals.messages[i] = side.messages[i].load(std::memory_order_relaxed);
    totals.bytes[i] = side.bytes[i].load(std::memory_order_relaxed);
  }

  return totals;
}

void TrafficCounters::countOut(const MessageType type, const std::size_t payloadBytes,
                               const std::size_t recipients) noexcept
{
  add(m_out, type, recipients, (static_cast<uint64_t>(payloadBytes) + 1) * recipients);
}

void TrafficCounters::countIn(const MessageType type, const std::size_t payloadBytes) noexcept
{
  add(m_in, type, 1, static_cast<uint64_t>(payloadBytes) + 1);
}

void TrafficCounters::countRefused() noexcept
{
  m_refused.fetch_add(1, std::memory_order_relaxed);
}

TrafficTotals TrafficCounters::totalOut() const noexcept
{
  auto totals = read(m_out);
  totals.refused = m_refused.load(std::memory_order_relaxed);
  return totals;
}

TrafficTotals TrafficCounters::totalIn() const noexcept
{
  return read(m_in);
}

}
