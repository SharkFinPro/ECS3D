#include "LogFilter.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>

LogFilter::LogFilter()
{
  levels.fill(true);
  categories.fill(true);
}

namespace {
  // ASCII-only, locale-independent lowercasing: the search box is meant to match "ERR" against "error"
  // regardless of the user's locale, not to fold non-ASCII case rules.
  char toAsciiLower(const char c)
  {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  }

  std::string toAsciiLower(std::string text)
  {
    std::ranges::transform(text, text.begin(), [](const char c) { return toAsciiLower(c); });
    return text;
  }
}

bool LogFilter::matches(const LogEntry& entry) const
{
  if (!levels[static_cast<std::size_t>(entry.level)])
  {
    return false;
  }

  if (!categories[static_cast<std::size_t>(entry.category)])
  {
    return false;
  }

  if (search.empty())
  {
    return true;
  }

  return toAsciiLower(entry.message).find(toAsciiLower(search)) != std::string::npos;
}

std::string formatEntry(const LogEntry& entry)
{
  const std::time_t t = std::chrono::system_clock::to_time_t(entry.time);

  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif

  char timeBuffer[16];
  std::strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", &tm);

  return std::string(timeBuffer) + " [" + std::string(toString(entry.level)) + "]["
       + std::string(toString(entry.category)) + "] " + entry.message;
}
