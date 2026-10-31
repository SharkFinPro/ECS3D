#include <gtest/gtest.h>

#include "LogFilter.h"

#include <chrono>
#include <regex>
#include <string>

namespace {
  LogEntry makeEntry(const LogLevel level, const LogCategory category, const std::string& message)
  {
    return LogEntry{ std::chrono::system_clock::now(), level, category, message };
  }
}

TEST(LogFilterTest, DefaultFilterMatchesEveryLevelAndCategory)
{
  const LogFilter filter;

  for (const auto level : { LogLevel::trace, LogLevel::debug, LogLevel::info, LogLevel::warn, LogLevel::error })
  {
    EXPECT_TRUE(filter.matches(makeEntry(level, LogCategory::engine, "hello")));
  }

  for (const auto category : { LogCategory::engine, LogCategory::assets, LogCategory::physics, LogCategory::net,
                                LogCategory::script, LogCategory::editor, LogCategory::server,
                                LogCategory::client })
  {
    EXPECT_TRUE(filter.matches(makeEntry(LogLevel::info, category, "hello")));
  }
}

TEST(LogFilterTest, SwitchingOffALevelExcludesExactlyThatLevel)
{
  LogFilter filter;
  filter.levels[static_cast<std::size_t>(LogLevel::warn)] = false;

  EXPECT_FALSE(filter.matches(makeEntry(LogLevel::warn, LogCategory::engine, "warned")));

  // Positive control: another level is unaffected by the toggle above.
  EXPECT_TRUE(filter.matches(makeEntry(LogLevel::info, LogCategory::engine, "informed")));
}

TEST(LogFilterTest, SwitchingOffACategoryExcludesExactlyThatCategory)
{
  LogFilter filter;
  filter.categories[static_cast<std::size_t>(LogCategory::net)] = false;

  EXPECT_FALSE(filter.matches(makeEntry(LogLevel::info, LogCategory::net, "packet")));

  // Positive control: another category is unaffected by the toggle above.
  EXPECT_TRUE(filter.matches(makeEntry(LogLevel::info, LogCategory::physics, "step")));
}

TEST(LogFilterTest, SearchIsACaseInsensitiveSubstring)
{
  LogFilter filter;
  filter.search = "ERR";

  EXPECT_TRUE(filter.matches(makeEntry(LogLevel::error, LogCategory::engine, "an error occurred")));

  filter.search = "error";
  EXPECT_TRUE(filter.matches(makeEntry(LogLevel::error, LogCategory::engine, "An ERROR occurred")));
}

TEST(LogFilterTest, EmptySearchMatchesEverything)
{
  LogFilter filter;
  filter.search.clear();

  EXPECT_TRUE(filter.matches(makeEntry(LogLevel::trace, LogCategory::client, "anything at all")));
}

TEST(LogFilterTest, SearchWithNoHitExcludesTheEntry)
{
  LogFilter filter;
  filter.search = "nope";

  EXPECT_FALSE(filter.matches(makeEntry(LogLevel::info, LogCategory::engine, "an error occurred")));

  // Positive control alongside: a search that does hit still matches, so the false above reflects the
  // substring miss rather than some other break in matches().
  filter.search = "error";
  EXPECT_TRUE(filter.matches(makeEntry(LogLevel::info, LogCategory::engine, "an error occurred")));
}

TEST(LogFilterTest, FormatEntryLaysOutClockLevelCategoryAndMessage)
{
  const auto entry = makeEntry(LogLevel::warn, LogCategory::net, "connection dropped");

  // The clock is local time, so only its shape is pinned.
  EXPECT_TRUE(std::regex_match(formatEntry(entry),
                               std::regex(R"(\d\d:\d\d:\d\d \[warn\]\[net\] connection dropped)")))
      << formatEntry(entry);
}
