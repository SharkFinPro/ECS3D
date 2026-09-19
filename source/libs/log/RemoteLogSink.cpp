#include "RemoteLogSink.h"
#include <algorithm>
#include <utility>

RemoteLogSink::RemoteLogSink(const std::size_t capacity)
  : m_capacity(capacity)
{
}

void RemoteLogSink::write(const LogEntry& entry)
{
  const std::lock_guard lock(m_mutex);

  if (m_entries.size() >= m_capacity)
  {
    m_entries.pop_front();
    ++m_droppedSinceLastDrain;
  }

  m_entries.push_back(entry);
}

RemoteLogSink::Drained RemoteLogSink::drain(const std::size_t maxCount)
{
  const std::lock_guard lock(m_mutex);

  Drained result;
  result.dropped = std::exchange(m_droppedSinceLastDrain, std::uint64_t{ 0 });

  const auto count = std::min(maxCount, m_entries.size());
  result.entries.reserve(count);
  for (std::size_t i = 0; i < count; ++i)
  {
    result.entries.push_back(std::move(m_entries.front()));
    m_entries.pop_front();
  }

  return result;
}
