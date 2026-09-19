#include "ServerLog.h"
#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace net {

Message packServerLog(const std::vector<LogEntry>& entries, const std::uint64_t dropped)
{
  Message message(MessageType::serverLog);
  message.write(dropped);
  message.write(static_cast<uint32_t>(entries.size()));

  for (const auto& entry : entries)
  {
    message.write(entry.level);
    message.write(entry.category);
    message.write(static_cast<int64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(entry.time.time_since_epoch()).count()));
    message.writeString(entry.message);
  }

  return message;
}

ServerLogBatch unpackServerLog(const Message& message)
{
  MessageReader reader(message);

  ServerLogBatch batch;
  batch.dropped = reader.read<uint64_t>();
  const auto count = reader.read<uint32_t>();

  // Each entry is at least a level, a category, a timestamp and an (empty) string's length prefix; a
  // count past what the remaining payload could actually hold can only come from a corrupt or hostile
  // message, so it is refused here rather than handed to reserve() as an allocation size.
  constexpr std::size_t minEntryBytes = sizeof(LogLevel) + sizeof(LogCategory) + sizeof(int64_t) + sizeof(uint32_t);
  if (count > reader.remaining() / minEntryBytes)
  {
    throw std::runtime_error("serverLog message underflow: entry count exceeds what the payload can hold");
  }

  batch.entries.reserve(count);
  for (uint32_t i = 0; i < count; ++i)
  {
    LogEntry entry;
    entry.level = reader.read<LogLevel>();
    entry.category = reader.read<LogCategory>();
    const auto millis = reader.read<int64_t>();
    entry.time = std::chrono::system_clock::time_point(std::chrono::milliseconds(millis));
    entry.message = reader.readString();
    batch.entries.push_back(std::move(entry));
  }

  return batch;
}

}
