#include <gtest/gtest.h>

#include <RemoteLogSink.h>

#include <chrono>
#include <cstdint>
#include <string>

namespace {
  LogEntry entry(const std::string& message, const LogLevel level = LogLevel::info,
                 const LogCategory category = LogCategory::server)
  {
    return LogEntry{ std::chrono::system_clock::now(), level, category, message };
  }
}

TEST(RemoteLogSink, DrainReturnsWhatWasWrittenInOrder)
{
  RemoteLogSink sink(10);

  sink.write(entry("first"));
  sink.write(entry("second"));

  const auto drained = sink.drain(10);

  ASSERT_EQ(drained.entries.size(), 2u);
  EXPECT_EQ(drained.entries[0].message, "first");
  EXPECT_EQ(drained.entries[1].message, "second");
  EXPECT_EQ(drained.dropped, 0u);
}

TEST(RemoteLogSink, DrainLeavesAnythingPastMaxCountQueuedForNextTime)
{
  RemoteLogSink sink(10);

  sink.write(entry("a"));
  sink.write(entry("b"));
  sink.write(entry("c"));

  // Caps what a single call (one server tick) can take, so a burst of entries costs bounded work per
  // call instead of one send sized by however much piled up.
  const auto first = sink.drain(2);
  ASSERT_EQ(first.entries.size(), 2u);
  EXPECT_EQ(first.entries[0].message, "a");
  EXPECT_EQ(first.entries[1].message, "b");

  const auto second = sink.drain(2);
  ASSERT_EQ(second.entries.size(), 1u);
  EXPECT_EQ(second.entries[0].message, "c");
}

TEST(RemoteLogSink, OverflowingCapacityDropsTheOldestAndCountsIt)
{
  constexpr std::size_t capacity = 3;
  RemoteLogSink sink(capacity);

  sink.write(entry("1"));
  sink.write(entry("2"));
  sink.write(entry("3"));
  // Past capacity: "1" is evicted rather than the queue growing without limit.
  sink.write(entry("4"));
  sink.write(entry("5"));

  const auto drained = sink.drain(capacity);

  ASSERT_EQ(drained.entries.size(), capacity);
  EXPECT_EQ(drained.entries[0].message, "3");
  EXPECT_EQ(drained.entries[1].message, "4");
  EXPECT_EQ(drained.entries[2].message, "5");

  // Positive control: exactly the two entries a nobody-is-draining storm actually evicted, not zero and
  // not every entry ever written.
  EXPECT_EQ(drained.dropped, 2u);
}

TEST(RemoteLogSink, DrainResetsTheDropCountEvenWithNothingElseToTake)
{
  RemoteLogSink sink(1);

  sink.write(entry("kept"));
  sink.write(entry("evicts kept"));

  const auto first = sink.drain(10);
  ASSERT_EQ(first.entries.size(), 1u);
  EXPECT_EQ(first.dropped, 1u);

  // The drop was already reported; a drain with nothing new to say must not report it again.
  const auto second = sink.drain(10);
  EXPECT_TRUE(second.entries.empty());
  EXPECT_EQ(second.dropped, 0u);
}

TEST(RemoteLogSink, DrainOfAnEmptySinkReturnsNothing)
{
  RemoteLogSink sink(10);

  const auto drained = sink.drain(10);

  EXPECT_TRUE(drained.entries.empty());
  EXPECT_EQ(drained.dropped, 0u);
}

TEST(RemoteLogSink, PreservesLevelAndCategoryPerEntry)
{
  RemoteLogSink sink(10);

  sink.write(entry("bounce", LogLevel::warn, LogCategory::physics));
  sink.write(entry("crash", LogLevel::error, LogCategory::script));

  const auto drained = sink.drain(10);

  ASSERT_EQ(drained.entries.size(), 2u);
  EXPECT_EQ(drained.entries[0].level, LogLevel::warn);
  EXPECT_EQ(drained.entries[0].category, LogCategory::physics);
  EXPECT_EQ(drained.entries[1].level, LogLevel::error);
  EXPECT_EQ(drained.entries[1].category, LogCategory::script);
}

TEST(RemoteLogSink, AMessageOverTheCapIsTruncatedAtWriteTime)
{
  RemoteLogSink sink(10);

  // One call handing a message far longer than any real log line - a script dumping a huge string, say -
  // must not be able to queue it whole.
  const std::string huge(RemoteLogSink::maxMessageBytes + 1000, 'x');
  sink.write(entry(huge));

  const auto drained = sink.drain(10);

  ASSERT_EQ(drained.entries.size(), 1u);
  // Kept up to the cap, plus a marker rather than being silently cut off with no sign anything was lost.
  EXPECT_LE(drained.entries[0].message.size(), RemoteLogSink::maxMessageBytes + 32);
  EXPECT_LT(drained.entries[0].message.size(), huge.size());
  EXPECT_NE(drained.entries[0].message.find("truncated"), std::string::npos);
}

TEST(RemoteLogSink, AMessageAtOrUnderTheCapIsNotTruncated)
{
  RemoteLogSink sink(10);

  // Positive control for the previous test: the cap only bites once a message actually exceeds it.
  const std::string exact(RemoteLogSink::maxMessageBytes, 'x');
  sink.write(entry(exact));

  const auto drained = sink.drain(10);

  ASSERT_EQ(drained.entries.size(), 1u);
  EXPECT_EQ(drained.entries[0].message, exact);
}

TEST(RemoteLogSink, DrainStopsEarlyOnceTheByteBudgetWouldBeExceeded)
{
  RemoteLogSink sink(10);

  const std::string mid(1000, 'a');
  sink.write(entry(mid));
  sink.write(entry(mid));
  sink.write(entry(mid));

  // A budget that fits the first entry (plus its wire overhead) but not two of them: the batch must stop
  // there rather than pack a message a transport's own size limit could refuse outright.
  const auto drained = sink.drain(10, mid.size() + 64);

  ASSERT_EQ(drained.entries.size(), 1u);
  EXPECT_EQ(drained.entries[0].message, mid);

  // What the first drain left queued is still there for the next one - it was deferred, not dropped.
  const auto second = sink.drain(10, mid.size() + 64);
  ASSERT_EQ(second.entries.size(), 1u);
  EXPECT_EQ(second.dropped, 0u);
}

TEST(RemoteLogSink, DrainAlwaysTakesAtLeastOneEntryEvenUnderABudgetItAloneExceeds)
{
  RemoteLogSink sink(10);

  const std::string large(5000, 'a');
  sink.write(entry(large));
  sink.write(entry("b"));

  // A budget far smaller than even the first entry alone: draining nothing would stall the queue forever
  // rather than merely deferring the second entry to the next call.
  const auto drained = sink.drain(10, 10);

  ASSERT_EQ(drained.entries.size(), 1u);
  EXPECT_EQ(drained.entries[0].message, large);
}

TEST(RemoteLogSink, ANonLimitingByteBudgetTakesEverythingMaxCountAllows)
{
  RemoteLogSink sink(10);

  sink.write(entry("a"));
  sink.write(entry("b"));
  sink.write(entry("c"));

  // The default maxBytes (used when a caller only cares about the entry-count cap) must not itself
  // truncate a batch that easily fits.
  const auto drained = sink.drain(10);

  EXPECT_EQ(drained.entries.size(), 3u);
}
