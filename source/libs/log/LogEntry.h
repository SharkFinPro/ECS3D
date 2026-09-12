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

[[nodiscard]] std::string_view toString(LogCategory category);

struct LogEntry {
  std::chrono::system_clock::time_point time;
  LogLevel level;
  LogCategory category;
  std::string message;
};

#endif  // LOGENTRY_H
