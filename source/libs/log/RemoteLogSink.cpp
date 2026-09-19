#include "RemoteLogSink.h"
#include <algorithm>
#include <string>
#include <utility>

namespace {
  // A generous, fixed estimate of what packServerLog spends per entry besides the message text itself
  // (level, category, timestamp, length prefix) - not exact, just enough margin that drain()'s byte
  // budget is a real ceiling on the packed message rather than an approximation an entry can slip past.
  constexpr std::size_t wireOverheadPerEntry = 32;

  const std::string truncationMarker = " ... [truncated]";
}

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

  // A single call handing a message far longer than anything a log line should be (a script dumping a
  // huge string, say) must not be able to dominate this sink's memory or, downstream, a drain's whole
  // byte budget on its own.
  if (entry.message.size() <= maxMessageBytes)
  {
    m_entries.push_back(entry);
  }
  else
  {
    LogEntry truncated = entry;
    truncated.message.resize(maxMessageBytes);
    truncated.message += truncationMarker;
    m_entries.push_back(std::move(truncated));
  }
}

RemoteLogSink::Drained RemoteLogSink::drain(const std::size_t maxCount, const std::size_t maxBytes)
{
  const std::lock_guard lock(m_mutex);

  Drained result;
  result.dropped = std::exchange(m_droppedSinceLastDrain, std::uint64_t{ 0 });

  const auto count = std::min(maxCount, m_entries.size());
  result.entries.reserve(count);

  std::size_t bytesTaken = 0;
  for (std::size_t i = 0; i < count; ++i)
  {
    const auto entryBytes = wireOverheadPerEntry + m_entries.front().message.size();

    // The byte budget stops the batch from growing further, but never before at least one entry is
    // taken - otherwise a queue whose very first entry is already large (though write() caps how large)
    // would drain nothing at all, tick after tick.
    if (i > 0 && bytesTaken + entryBytes > maxBytes)
    {
      break;
    }

    bytesTaken += entryBytes;
    result.entries.push_back(std::move(m_entries.front()));
    m_entries.pop_front();
  }

  return result;
}
