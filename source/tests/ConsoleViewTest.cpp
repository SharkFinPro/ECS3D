#include <ConsoleView.h>
#include <RingBufferSink.h>
#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {
  LogEntry entry(const LogLevel level, const std::string& message, const LogCategory category = LogCategory::engine)
  {
    return { {}, level, category, message };
  }

  int levelCountOf(const ConsoleView& view, const LogLevel level)
  {
    return view.levelCounts[static_cast<std::size_t>(level)];
  }

  std::vector<std::string> messages(const ConsoleView& view)
  {
    std::vector<std::string> result;
    for (const auto& row : view.rows)
    {
      result.push_back(row.message);
    }
    return result;
  }

  ConsoleView viewOf(const RingBufferSink& sink, const std::uint64_t clearedBefore, const LogFilter& filter = {})
  {
    const auto currentSequence = sink.sequence();
    return buildConsoleView(sink.snapshot(), currentSequence, clearedBefore, filter);
  }
}

TEST(ConsoleViewTest, WithoutAClearEveryEntryIsShownAndCountedByLevel)
{
  RingBufferSink sink;
  sink.write(entry(LogLevel::info, "a"));
  sink.write(entry(LogLevel::error, "b"));
  sink.write(entry(LogLevel::info, "c"));

  const auto view = viewOf(sink, 0);

  EXPECT_EQ(messages(view), (std::vector<std::string>{ "a", "b", "c" }));
  EXPECT_EQ(levelCountOf(view, LogLevel::info), 2);
  EXPECT_EQ(levelCountOf(view, LogLevel::error), 1);
  EXPECT_EQ(levelCountOf(view, LogLevel::warn), 0);
}

TEST(ConsoleViewTest, ClearHidesEntriesUpToTheClearAndKeepsLaterOnes)
{
  RingBufferSink sink;
  sink.write(entry(LogLevel::info, "old1"));
  sink.write(entry(LogLevel::error, "old2"));

  const auto clearedBefore = sink.sequence();

  sink.write(entry(LogLevel::info, "new1"));
  sink.write(entry(LogLevel::warn, "new2"));

  const auto view = viewOf(sink, clearedBefore);

  EXPECT_EQ(messages(view), (std::vector<std::string>{ "new1", "new2" }));
  EXPECT_EQ(levelCountOf(view, LogLevel::info), 1);
  EXPECT_EQ(levelCountOf(view, LogLevel::warn), 1);
  EXPECT_EQ(levelCountOf(view, LogLevel::error), 0);
}

TEST(ConsoleViewTest, LevelCountsIgnoreTheLevelCategoryAndSearchFilters)
{
  RingBufferSink sink;
  sink.write(entry(LogLevel::info, "alpha", LogCategory::engine));
  sink.write(entry(LogLevel::error, "beta", LogCategory::net));
  sink.write(entry(LogLevel::error, "gamma", LogCategory::physics));

  const auto unfiltered = viewOf(sink, 0);
  ASSERT_EQ(unfiltered.rows.size(), 3u);

  LogFilter levelOff;
  levelOff.levels[static_cast<std::size_t>(LogLevel::error)] = false;
  const auto withoutErrors = viewOf(sink, 0, levelOff);
  EXPECT_EQ(messages(withoutErrors), (std::vector<std::string>{ "alpha" }));
  EXPECT_EQ(withoutErrors.levelCounts, unfiltered.levelCounts);

  LogFilter categoryOff;
  categoryOff.categories[static_cast<std::size_t>(LogCategory::net)] = false;
  const auto withoutNet = viewOf(sink, 0, categoryOff);
  EXPECT_EQ(messages(withoutNet), (std::vector<std::string>{ "alpha", "gamma" }));
  EXPECT_EQ(withoutNet.levelCounts, unfiltered.levelCounts);

  LogFilter searching;
  searching.search = "gamma";
  const auto searched = viewOf(sink, 0, searching);
  EXPECT_EQ(messages(searched), (std::vector<std::string>{ "gamma" }));
  EXPECT_EQ(searched.levelCounts, unfiltered.levelCounts);
}

TEST(ConsoleViewTest, ClearInsideAWrappedRingBufferSplitsOnTheRightEntry)
{
  RingBufferSink sink(3);
  for (int i = 1; i <= 5; ++i)
  {
    sink.write(entry(LogLevel::info, "m" + std::to_string(i)));
  }

  // Retained are m3, m4, m5 (sequences 3..5); clearing at 4 hides m3 and m4.
  const auto view = viewOf(sink, 4);

  EXPECT_EQ(messages(view), (std::vector<std::string>{ "m5" }));
  EXPECT_EQ(levelCountOf(view, LogLevel::info), 1);

  const auto whole = viewOf(sink, 0);
  EXPECT_EQ(messages(whole), (std::vector<std::string>{ "m3", "m4", "m5" }));
}

TEST(ConsoleViewTest, ClearNewerThanEverythingLeavesAnEmptyView)
{
  RingBufferSink sink;
  sink.write(entry(LogLevel::info, "a"));
  sink.write(entry(LogLevel::error, "b"));

  ASSERT_EQ(viewOf(sink, 0).rows.size(), 2u);

  const auto view = viewOf(sink, sink.sequence());

  EXPECT_TRUE(view.rows.empty());
  for (const int count : view.levelCounts)
  {
    EXPECT_EQ(count, 0);
  }
}
