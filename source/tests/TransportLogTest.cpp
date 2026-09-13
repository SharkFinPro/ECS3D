#include <gtest/gtest.h>

#include "TransportLog.h"

#include "Log.h"
#include "RingBufferSink.h"

#include <cstddef>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
  // Global state (the sink list and minimum level) is shared across every test in the suite, so each
  // test must leave it exactly as it found it or test order starts to matter.
  class TransportLogTest : public testing::Test {
  protected:
    void SetUp() override
    {
      m_sink = std::make_shared<RingBufferSink>();
      Log::addSink(m_sink);
      Log::setMinimumLevel(LogLevel::trace);
    }

    void TearDown() override
    {
      Log::removeSink(m_sink);
      Log::setMinimumLevel(LogLevel::info);
    }

    std::shared_ptr<RingBufferSink> m_sink;
  };
}

TEST_F(TransportLogTest, MapsEachInRangeLevelToTheMatchingLogLevel)
{
  net::transportLog(0, "trace message");
  net::transportLog(1, "debug message");
  net::transportLog(2, "info message");
  net::transportLog(3, "warn message");
  net::transportLog(4, "error message");

  const auto entries = m_sink->snapshot();
  ASSERT_EQ(entries.size(), 5u);

  const std::pair<LogLevel, std::string> expected[] = {
    { LogLevel::trace, "trace message" },
    { LogLevel::debug, "debug message" },
    { LogLevel::info, "info message" },
    { LogLevel::warn, "warn message" },
    { LogLevel::error, "error message" },
  };

  for (std::size_t i = 0; i < entries.size(); ++i)
  {
    EXPECT_EQ(entries[i].level, expected[i].first);
    EXPECT_EQ(entries[i].category, LogCategory::net);
    EXPECT_EQ(entries[i].message, expected[i].second);
  }
}

TEST_F(TransportLogTest, MapsAnOutOfRangeLevelToInfo)
{
  net::transportLog(-1, "too low");
  net::transportLog(99, "too high");

  const auto entries = m_sink->snapshot();
  ASSERT_EQ(entries.size(), 2u);

  EXPECT_EQ(entries[0].level, LogLevel::info);
  EXPECT_EQ(entries[0].category, LogCategory::net);
  EXPECT_EQ(entries[0].message, "too low");

  EXPECT_EQ(entries[1].level, LogLevel::info);
  EXPECT_EQ(entries[1].message, "too high");
}

TEST_F(TransportLogTest, IgnoresANullMessage)
{
  net::transportLog(2, nullptr);
  EXPECT_TRUE(m_sink->snapshot().empty());

  // Positive control: a real call right after still gets through, so the empty result above is about
  // the null guard rather than transportLog having stopped writing entirely.
  net::transportLog(2, "still works");
  ASSERT_EQ(m_sink->snapshot().size(), 1u);
  EXPECT_EQ(m_sink->snapshot()[0].message, "still works");
}

TEST_F(TransportLogTest, ConcurrentCallersLoseNothing)
{
  constexpr int threadCount = 4;
  constexpr int perThread = 100;

  std::vector<std::thread> threads;
  threads.reserve(threadCount);
  for (int t = 0; t < threadCount; ++t)
  {
    threads.emplace_back([t] {
      for (int i = 0; i < perThread; ++i)
      {
        const auto message = std::to_string(t) + ":" + std::to_string(i);
        net::transportLog(2, message.c_str());
      }
    });
  }

  for (auto& thread : threads)
  {
    thread.join();
  }

  const auto entries = m_sink->snapshot();
  ASSERT_EQ(entries.size(), static_cast<std::size_t>(threadCount) * perThread);

  std::vector<std::vector<bool>> seen(threadCount, std::vector<bool>(perThread, false));
  for (const auto& entry : entries)
  {
    EXPECT_EQ(entry.level, LogLevel::info);
    EXPECT_EQ(entry.category, LogCategory::net);

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
}
