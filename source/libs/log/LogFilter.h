#ifndef LOGFILTER_H
#define LOGFILTER_H

#include "LogEntry.h"
#include <array>
#include <string>

// What a console view shows of a log: which levels and categories, and an optional case-insensitive
// substring of the message. Lives in the log library so it can be tested without the editor.
struct LogFilter {
  std::array<bool, levelCount> levels{};
  std::array<bool, categoryCount> categories{};
  std::string search;

  LogFilter();

  [[nodiscard]] bool matches(const LogEntry& entry) const;
};

// "HH:MM:SS [level][category] message" in local time - used both for console panel rows and for copy.
[[nodiscard]] std::string formatEntry(const LogEntry& entry);

#endif  // LOGFILTER_H
