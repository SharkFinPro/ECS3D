#include "LogBindings.h"
#include "Log.h"
#include <string>

namespace {
  // The 0..4 mapping below assumes this exact underlying order; a LogLevel reorder must fail the build
  // rather than silently mislabel every script log call.
  static_assert(static_cast<int>(LogLevel::trace) == 0);
  static_assert(static_cast<int>(LogLevel::debug) == 1);
  static_assert(static_cast<int>(LogLevel::info) == 2);
  static_assert(static_cast<int>(LogLevel::warn) == 3);
  static_assert(static_cast<int>(LogLevel::error) == 4);

  LogLevel toLogLevel(const int level)
  {
    switch (level)
    {
      case 0: return LogLevel::trace;
      case 1: return LogLevel::debug;
      case 2: return LogLevel::info;
      case 3: return LogLevel::warn;
      case 4: return LogLevel::error;
      default: return LogLevel::info;
    }
  }
}

LogBindings LogBindingsProvider::getBindings()
{
  return LogBindings {
    .write = &bindWrite
  };
}

void LogBindingsProvider::bindWrite(const int level, const char* message)
{
  // Called from managed code across the unmanaged boundary - no exception may escape (a std::string
  // allocation can throw).
  try
  {
    if (!message)
    {
      return;
    }

    Log::write(toLogLevel(level), LogCategory::script, std::string(message));
  }
  catch (...)
  {
  }
}
