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

  // A plain byte-count resize() can land in the middle of a multi-byte UTF-8 sequence (a log message is
  // arbitrary text - a script string, a path, an exception message - so this is not hypothetical), which
  // would hand packServerLog/writeString a string that is no longer valid UTF-8. Finds the start of
  // whichever sequence the boundary at `length` falls inside (walking back over continuation bytes,
  // 10xxxxxx, which cannot start a sequence) and keeps that sequence only if it ends at or before
  // `length` - otherwise the whole thing is dropped, so a sequence is either kept whole or cut whole,
  // never left half-written.
  [[nodiscard]] std::size_t utf8SafeTruncationLength(const std::string& text, const std::size_t length)
  {
    if (length >= text.size())
    {
      return text.size();
    }

    if (length == 0)
    {
      return 0;
    }

    std::size_t leadIndex = length - 1;
    while (leadIndex > 0 && (static_cast<unsigned char>(text[leadIndex]) & 0xC0) == 0x80)
    {
      --leadIndex;
    }

    const auto lead = static_cast<unsigned char>(text[leadIndex]);
    std::size_t sequenceLength;
    if ((lead & 0x80) == 0x00)
    {
      sequenceLength = 1;
    }
    else if ((lead & 0xE0) == 0xC0)
    {
      sequenceLength = 2;
    }
    else if ((lead & 0xF0) == 0xE0)
    {
      sequenceLength = 3;
    }
    else if ((lead & 0xF8) == 0xF0)
    {
      sequenceLength = 4;
    }
    else
    {
      // Not a valid UTF-8 lead byte either (a stray continuation byte with nothing valid before it, or a
      // byte no UTF-8 sequence starts with) - drop it too rather than keep something that was never valid.
      return leadIndex;
    }

    return leadIndex + sequenceLength <= length ? length : leadIndex;
  }
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
    truncated.message.resize(utf8SafeTruncationLength(truncated.message, maxMessageBytes));
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
