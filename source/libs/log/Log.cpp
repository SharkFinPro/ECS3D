#include "Log.h"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <utility>
#include <vector>

namespace {
  // Function-local statics only fix initialization order; destruction order across translation units
  // is still unspecified, and the C# transport can log from socket threads that outlive main. Leaking
  // this storage means it is simply never destroyed, so a late call is always safe.
  std::mutex& logMutex()
  {
    static auto* mutex = new std::mutex();
    return *mutex;
  }

  std::vector<std::shared_ptr<LogSink>>& logSinks()
  {
    static auto* sinks = new std::vector<std::shared_ptr<LogSink>>();
    return *sinks;
  }

  // isEnabled() reads this without the lock from any thread, so it must be atomic. Left as an ordinary
  // static rather than leaked storage: it is trivially destructible, so there is no destruction-order
  // hazard for it to hide.
  std::atomic<LogLevel> g_minimumLevel = LogLevel::info;
}

void Log::addSink(std::shared_ptr<LogSink> sink)
{
  const std::lock_guard lock(logMutex());

  auto& sinks = logSinks();
  if (std::ranges::find(sinks, sink) != sinks.end())
  {
    return;
  }

  sinks.push_back(std::move(sink));
}

void Log::removeSink(const std::shared_ptr<LogSink>& sink)
{
  const std::lock_guard lock(logMutex());

  std::erase(logSinks(), sink);
}

void Log::setMinimumLevel(const LogLevel level)
{
  g_minimumLevel = level;
}

bool Log::isEnabled(const LogLevel level)
{
  return level >= g_minimumLevel.load();
}

void Log::write(const LogLevel level, const LogCategory category, std::string message)
{
  if (!isEnabled(level))
  {
    return;
  }

  // Held across the sink calls below rather than released first: a sink must never log (see
  // LogSink.h), so re-entering write() on this thread can't happen and the lock can't deadlock.
  const std::lock_guard lock(logMutex());

  auto& sinks = logSinks();
  if (sinks.empty())
  {
    return;
  }

  const LogEntry entry{ std::chrono::system_clock::now(), level, category, std::move(message) };

  for (const auto& sink : sinks)
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
