#include "RingBufferSink.h"

RingBufferSink::RingBufferSink(const std::size_t capacity)
  : m_capacity(capacity)
{
}

void RingBufferSink::write(const LogEntry& entry)
{
  const std::lock_guard lock(m_mutex);

  m_entries.push_back(entry);
  ++m_sequence;

  while (m_entries.size() > m_capacity)
  {
    m_entries.pop_front();
  }
}

std::vector<LogEntry> RingBufferSink::snapshot() const
{
  const std::lock_guard lock(m_mutex);

  return { m_entries.begin(), m_entries.end() };
}

std::uint64_t RingBufferSink::sequence() const
{
  const std::lock_guard lock(m_mutex);

  return m_sequence;
}
