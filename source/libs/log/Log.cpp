#include "Log.h"
#include <mutex>
#include <vector>

namespace {
  std::mutex g_mutex;
  std::vector<std::shared_ptr<LogSink>> g_sinks;
  LogLevel g_minimumLevel = LogLevel::info;
}

void Log::addSink(std::shared_ptr<LogSink> sink)
{
  const std::lock_guard lock(g_mutex);

  g_sinks.push_back(std::move(sink));
}

void Log::removeSink(const std::shared_ptr<LogSink>& sink)
{
  const std::lock_guard lock(g_mutex);

  std::erase(g_sinks, sink);
}

void Log::setMinimumLevel(const LogLevel level)
{
  const std::lock_guard lock(g_mutex);

  g_minimumLevel = level;
}

void Log::write(const LogLevel level, const LogCategory category, std::string message)
{
  // Held across the sink calls below rather than released first: a sink must never log (see
  // LogSink.h), so re-entering write() on this thread can't happen and the lock can't deadlock.
  const std::lock_guard lock(g_mutex);

  if (level < g_minimumLevel || g_sinks.empty())
  {
    return;
  }

  const LogEntry entry{ std::chrono::system_clock::now(), level, category, std::move(message) };

  for (const auto& sink : g_sinks)
  {
    sink->write(entry);
  }
}

void Log::trace(const LogCategory category, std::string message)
{
  write(LogLevel::trace, category, std::move(message));
}

void Log::debug(const LogCategory category, std::string message)
{
  write(LogLevel::debug, category, std::move(message));
}

void Log::info(const LogCategory category, std::string message)
{
  write(LogLevel::info, category, std::move(message));
}

void Log::warn(const LogCategory category, std::string message)
{
  write(LogLevel::warn, category, std::move(message));
}

void Log::error(const LogCategory category, std::string message)
{
  write(LogLevel::error, category, std::move(message));
}
