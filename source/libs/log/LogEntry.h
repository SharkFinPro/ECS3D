#ifndef LOGENTRY_H
#define LOGENTRY_H

#include <chrono>
#include <string>
#include <string_view>

enum class LogLevel {
  trace,
  debug,
  info,
  warn,
  error
};

// Count of LogLevel values, for callers that size a per-level array (the console panel's filter).
inline constexpr int levelCount = static_cast<int>(LogLevel::error) + 1;

[[nodiscard]] std::string_view toString(LogLevel level);

// What part of the engine an entry came from, so a future console panel can filter by it.
enum class LogCategory {
  engine,
  assets,
  physics,
  net,
  script,
  editor,
  server,
  client
};

// Count of LogCategory values, for callers that size a per-category array (the console panel's filter).
inline constexpr int categoryCount = static_cast<int>(LogCategory::client) + 1;

[[nodiscard]] std::string_view toString(LogCategory category);

struct LogEntry {
  std::chrono::system_clock::time_point time;
  LogLevel level;
  LogCategory category;
  std::string message;
};

#endif  // LOGENTRY_H
