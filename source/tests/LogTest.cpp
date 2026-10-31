#include <gtest/gtest.h>

#include "ConsoleSink.h"
#include "Log.h"
#include "RingBufferSink.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {
  // Global state (the sink list and minimum level) is shared across every test in the suite, so each
  // test must leave it exactly as it found it or test order starts to matter.
  class LogTest : public testing::Test {
  protected:
    void TearDown() override
    {
      for (const auto& sink : m_addedSinks)
      {
        Log::removeSink(sink);
      }

      Log::setMinimumLevel(LogLevel::info);
    }

    std::shared_ptr<RingBufferSink> addRingBuffer(const std::size_t capacity = 2000)
    {
      auto sink = std::make_shared<RingBufferSink>(capacity);
      Log::addSink(sink);
      m_addedSinks.push_back(sink);

      return sink;
    }

    std::vector<std::shared_ptr<LogSink>> m_addedSinks;
  };
}

TEST_F(LogTest, DeliversEntriesToARingBufferInOrder)
{
  const auto sink = addRingBuffer();

  Log::write(LogLevel::info, LogCategory::physics, "first");
  Log::write(LogLevel::warn, LogCategory::net, "second");

  const auto entries = sink->snapshot();

  ASSERT_EQ(entries.size(), 2u);

  EXPECT_EQ(entries[0].level, LogLevel::info);
  EXPECT_EQ(entries[0].category, LogCategory::physics);
  EXPECT_EQ(entries[0].message, "first");

  EXPECT_EQ(entries[1].level, LogLevel::warn);
  EXPECT_EQ(entries[1].category, LogCategory::net);
  EXPECT_EQ(entries[1].message, "second");
}

TEST_F(LogTest, DropsEntriesBelowTheMinimumLevel)
{
  const auto sink = addRingBuffer();

  Log::setMinimumLevel(LogLevel::warn);

  Log::write(LogLevel::info, LogCategory::engine, "dropped");
  EXPECT_TRUE(sink->snapshot().empty());

  // Positive control: raising the level does not wedge delivery shut, so the empty result above is
  // actually about the level filter rather than some other break in the pipeline.
  Log::write(LogLevel::error, LogCategory::engine, "kept");
  ASSERT_EQ(sink->snapshot().size(), 1u);
  EXPECT_EQ(sink->snapshot()[0].message, "kept");

  Log::setMinimumLevel(LogLevel::trace);
  Log::write(LogLevel::debug, LogCategory::engine, "kept again");
  EXPECT_EQ(sink->snapshot().size(), 2u);
}

TEST_F(LogTest, KeepsOnlyTheNewestEntriesOnceCapacityIsExceeded)
{
  constexpr std::size_t capacity = 5;
  constexpr int extra = 3;

  const auto sink = addRingBuffer(capacity);

  for (int i = 0; i < static_cast<int>(capacity) + extra; ++i)
  {
    Log::write(LogLevel::info, LogCategory::engine, std::to_string(i));
  }

  const auto entries = sink->snapshot();

  ASSERT_EQ(entries.size(), capacity);
  for (std::size_t i = 0; i < capacity; ++i)
  {
    EXPECT_EQ(entries[i].message, std::to_string(static_cast<int>(i) + extra));
  }

  EXPECT_EQ(sink->sequence(), static_cast<std::uint64_t>(capacity) + extra);
}

TEST_F(LogTest, RemoveSinkStopsDeliveryToThatSinkAlone)
{
  const auto removed = addRingBuffer();
  const auto kept = addRingBuffer();

  Log::write(LogLevel::info, LogCategory::engine, "before removal");

  Log::removeSink(removed);
  // Removed manually above, so TearDown's removeSink on it is a harmless no-op.

  Log::write(LogLevel::info, LogCategory::engine, "after removal");

  EXPECT_EQ(removed->snapshot().size(), 1u);

  // Positive control: the sink that was not removed keeps receiving, so the count above reflects
  // removal rather than write() having stopped delivering entirely.
  EXPECT_EQ(kept->snapshot().size(), 2u);
}

TEST_F(LogTest, AddingTheSameSinkTwiceDeliversEachEntryOnce)
{
  const auto sink = addRingBuffer();
  Log::addSink(sink);

  const auto other = addRingBuffer();

  Log::write(LogLevel::info, LogCategory::engine, "once please");

  EXPECT_EQ(sink->snapshot().size(), 1u);

  // Positive control: a distinct sink registered around the same time still receives the entry, so
  // the count above reflects de-duplication rather than write() failing to deliver at all.
  EXPECT_EQ(other->snapshot().size(), 1u);
}

TEST_F(LogTest, IsEnabledFollowsTheMinimumLevelInBothDirections)
{
  Log::setMinimumLevel(LogLevel::warn);

  EXPECT_FALSE(Log::isEnabled(LogLevel::info));
  EXPECT_TRUE(Log::isEnabled(LogLevel::warn));
  EXPECT_TRUE(Log::isEnabled(LogLevel::error));

  Log::setMinimumLevel(LogLevel::trace);

  EXPECT_TRUE(Log::isEnabled(LogLevel::info));
  EXPECT_TRUE(Log::isEnabled(LogLevel::trace));
}

TEST_F(LogTest, AnEntryWrittenBeforeASinkIsAddedIsNeverDeliveredToIt)
{
  Log::write(LogLevel::error, LogCategory::server, "nowhere to go");

  const auto sink = addRingBuffer();
  EXPECT_TRUE(sink->snapshot().empty());

  // Positive control: the sink is live, so the empty result is about the earlier write.
  Log::write(LogLevel::error, LogCategory::server, "delivered");
  ASSERT_EQ(sink->snapshot().size(), 1u);
  EXPECT_EQ(sink->snapshot()[0].message, "delivered");
}

TEST_F(LogTest, TheConvenienceWritersUseTheirOwnLevel)
{
  const auto sink = addRingBuffer();
  Log::setMinimumLevel(LogLevel::trace);

  Log::trace(LogCategory::engine, "t");
  Log::debug(LogCategory::assets, "d");
  Log::info(LogCategory::physics, "i");
  Log::warn(LogCategory::net, "w");
  Log::error(LogCategory::script, "e");

  const auto entries = sink->snapshot();
  ASSERT_EQ(entries.size(), 5u);

  EXPECT_EQ(entries[0].level, LogLevel::trace);
  EXPECT_EQ(entries[0].category, LogCategory::engine);
  EXPECT_EQ(entries[0].message, "t");
  EXPECT_EQ(entries[1].level, LogLevel::debug);
  EXPECT_EQ(entries[1].category, LogCategory::assets);
  EXPECT_EQ(entries[1].message, "d");
  EXPECT_EQ(entries[2].level, LogLevel::info);
  EXPECT_EQ(entries[2].category, LogCategory::physics);
  EXPECT_EQ(entries[2].message, "i");
  EXPECT_EQ(entries[3].level, LogLevel::warn);
  EXPECT_EQ(entries[3].category, LogCategory::net);
  EXPECT_EQ(entries[3].message, "w");
  EXPECT_EQ(entries[4].level, LogLevel::error);
  EXPECT_EQ(entries[4].category, LogCategory::script);
  EXPECT_EQ(entries[4].message, "e");
}

TEST_F(LogTest, AConsoleSinkRoutesInfoToStdoutAndWarningsToStderr)
{
  ConsoleSink sink;

  testing::internal::CaptureStdout();
  testing::internal::CaptureStderr();

  sink.write(LogEntry{ std::chrono::system_clock::now(), LogLevel::debug, LogCategory::assets, "quiet" });
  sink.write(LogEntry{ std::chrono::system_clock::now(), LogLevel::info, LogCategory::engine, "hello" });
  sink.write(LogEntry{ std::chrono::system_clock::now(), LogLevel::warn, LogCategory::net, "careful" });
  sink.write(LogEntry{ std::chrono::system_clock::now(), LogLevel::error, LogCategory::server, "broken" });

  const std::string standardError = testing::internal::GetCapturedStderr();
  const std::string standardOutput = testing::internal::GetCapturedStdout();

  EXPECT_EQ(standardOutput, "[debug][assets] quiet\n[info][engine] hello\n");
  EXPECT_EQ(standardError, "[warn][net] careful\n[error][server] broken\n");
}

TEST_F(LogTest, ConcurrentSinkChangesDoNotDisturbWriters)
{
  constexpr int writerCount = 4;
  constexpr int perWriter = 400;

  const auto stable = addRingBuffer(static_cast<std::size_t>(writerCount) * perWriter);

  std::atomic<bool> done = false;
  std::vector<std::thread> threads;
  for (int t = 0; t < writerCount; ++t)
  {
    threads.emplace_back([t] {
      for (int i = 0; i < perWriter; ++i)
      {
        Log::write(LogLevel::info, LogCategory::engine, std::to_string(t) + ":" + std::to_string(i));
      }
    });
  }

  std::thread churn([&done] {
    while (!done)
    {
      const auto transient = std::make_shared<RingBufferSink>(4);
      Log::addSink(transient);
      Log::removeSink(transient);
    }
  });

  for (auto& thread : threads)
  {
    thread.join();
  }
  done = true;
  churn.join();

  EXPECT_EQ(stable->snapshot().size(), static_cast<std::size_t>(writerCount) * perWriter);
}

TEST_F(LogTest, ConcurrentWritersLoseNothing)
{
  constexpr int threadCount = 8;
  constexpr int perThread = 500;

  const auto sink = addRingBuffer(static_cast<std::size_t>(threadCount) * perThread);

  std::vector<std::thread> threads;
  threads.reserve(threadCount);
  for (int t = 0; t < threadCount; ++t)
  {
    threads.emplace_back([t] {
      for (int i = 0; i < perThread; ++i)
      {
        Log::write(LogLevel::info, LogCategory::engine, std::to_string(t) + ":" + std::to_string(i));
      }
    });
  }

  for (auto& thread : threads)
  {
    thread.join();
  }

  const auto entries = sink->snapshot();
  ASSERT_EQ(entries.size(), static_cast<std::size_t>(threadCount) * perThread);
  EXPECT_EQ(sink->sequence(), static_cast<std::uint64_t>(threadCount) * perThread);

  std::vector<std::vector<bool>> seen(threadCount, std::vector<bool>(perThread, false));
  for (const auto& entry : entries)
  {
    const auto separator = entry.message.find(':');
    ASSERT_NE(separator, std::string::npos);

    const int thread = std::stoi(entry.message.substr(0, separator));
    const int index = std::stoi(entry.message.substr(separator + 1));

    ASSERT_GE(thread, 0);
    ASSERT_LT(thread, threadCount);
    ASSERT_GE(index, 0);
    ASSERT_LT(index, perThread);

    ASSERT_FALSE(seen[thread][index]) << "duplicate entry for thread " << thread << " index " << index;
    seen[thread][index] = true;
  }

  for (const auto& threadSeen : seen)
  {
    EXPECT_TRUE(std::ranges::all_of(threadSeen, [](const bool value) { return value; }));
  }
}

TEST(LogEntryToString, NamesEveryLogLevel)
{
  EXPECT_EQ(toString(LogLevel::trace), "trace");
  EXPECT_EQ(toString(LogLevel::debug), "debug");
  EXPECT_EQ(toString(LogLevel::info), "info");
  EXPECT_EQ(toString(LogLevel::warn), "warn");
  EXPECT_EQ(toString(LogLevel::error), "error");
}

TEST(LogEntryToString, NamesEveryLogCategory)
{
  EXPECT_EQ(toString(LogCategory::engine), "engine");
  EXPECT_EQ(toString(LogCategory::assets), "assets");
  EXPECT_EQ(toString(LogCategory::physics), "physics");
  EXPECT_EQ(toString(LogCategory::net), "net");
  EXPECT_EQ(toString(LogCategory::script), "script");
  EXPECT_EQ(toString(LogCategory::editor), "editor");
  EXPECT_EQ(toString(LogCategory::server), "server");
  EXPECT_EQ(toString(LogCategory::client), "client");
}
