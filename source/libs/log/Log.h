#ifndef LOG_H
#define LOG_H

#include "LogEntry.h"
#include "LogSink.h"
#include <memory>
#include <string>

// Process-wide log facade. With no sinks registered, write() does nothing, so tests and embedders
// stay silent unless they opt in. Thread-safe: the C# net transport pushes from socket threads.
class Log {
public:
  static void addSink(std::shared_ptr<LogSink> sink);

  static void removeSink(const std::shared_ptr<LogSink>& sink);

  // Entries below this level are dropped before any sink sees them. Defaults to info.
  static void setMinimumLevel(LogLevel level);

  static void write(LogLevel level, LogCategory category, std::string message);

  static void trace(LogCategory category, std::string message);

  static void debug(LogCategory category, std::string message);

  static void info(LogCategory category, std::string message);

  static void warn(LogCategory category, std::string message);

  static void error(LogCategory category, std::string message);
};

#endif  // LOG_H
