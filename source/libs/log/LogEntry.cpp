#include "LogEntry.h"

std::string_view toString(const LogLevel level)
{
  switch (level)
  {
    case LogLevel::trace:
      return "trace";
    case LogLevel::debug:
      return "debug";
    case LogLevel::info:
      return "info";
    case LogLevel::warn:
      return "warn";
    case LogLevel::error:
      return "error";
  }

  return "unknown";
}

std::string_view toString(const LogCategory category)
{
  switch (category)
  {
    case LogCategory::engine:
      return "engine";
    case LogCategory::assets:
      return "assets";
    case LogCategory::physics:
      return "physics";
    case LogCategory::net:
      return "net";
    case LogCategory::script:
      return "script";
    case LogCategory::editor:
      return "editor";
    case LogCategory::server:
      return "server";
    case LogCategory::client:
      return "client";
  }

  return "unknown";
}
