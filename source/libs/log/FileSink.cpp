#include "FileSink.h"
#include "LogEntry.h"
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>
#include <system_error>

namespace {
  // ISO-8601 UTC with milliseconds, e.g. "2026-01-02T03:04:05.006Z". Avoids std::format and chrono's
  // stream formatting - neither is available on every CI standard library (macOS libc++ in particular).
  std::string formatTimestamp(const std::chrono::system_clock::time_point& time)
  {
    const auto seconds = std::chrono::floor<std::chrono::seconds>(time);
    const auto milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(time - seconds).count();
    const std::time_t t = std::chrono::system_clock::to_time_t(seconds);

    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif

    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &tm);

    char withMilliseconds[40];
    std::snprintf(withMilliseconds, sizeof(withMilliseconds), "%s.%03dZ", buffer,
                  static_cast<int>(milliseconds));

    return withMilliseconds;
  }
}

FileSink::FileSink(const std::filesystem::path& file)
{
  try
  {
    if (!file.parent_path().empty())
    {
      std::error_code error;
      std::filesystem::create_directories(file.parent_path(), error);
    }

    m_stream.open(file, std::ios::out | std::ios::trunc | std::ios::binary);
  }
  catch (...)
  {
  }
}

void FileSink::write(const LogEntry& entry)
{
  try
  {
    const std::lock_guard lock(m_mutex);

    if (!m_stream.is_open())
    {
      return;
    }

    m_stream << formatTimestamp(entry.time) << " [" << toString(entry.level) << "]["
              << toString(entry.category) << "] " << entry.message << "\n";
    m_stream.flush();

    // A failed write must not silence every later entry: drop this one and keep trying.
    if (m_stream.fail())
    {
      m_stream.clear();
    }
  }
  catch (...)
  {
  }
}

bool FileSink::isOpen() const
{
  const std::lock_guard lock(m_mutex);

  return m_stream.is_open();
}
