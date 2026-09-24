#include <gtest/gtest.h>

#include "Log.h"
#include "LogBindings.h"
#include "RingBufferSink.h"

#include <memory>
#include <string>
#include <vector>

namespace {
  // Global state (the sink list and minimum level) is shared across every test in the suite, so each
  // test must leave it exactly as it found it or test order starts to matter (see LogTest.cpp).
  class LogBindingsTest : public testing::Test {
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

TEST_F(LogBindingsTest, EachInRangeLevelWritesTheMatchingLogLevel)
{
  const LogBindings bindings = LogBindingsProvider::getBindings();

  bindings.write(0, "trace message");
  bindings.write(1, "debug message");
  bindings.write(2, "info message");
  bindings.write(3, "warn message");
  bindings.write(4, "error message");

  const auto entries = m_sink->snapshot();
  ASSERT_EQ(entries.size(), 5u);

  const LogLevel expectedLevels[] = { LogLevel::trace, LogLevel::debug, LogLevel::info, LogLevel::warn,
                                       LogLevel::error };
  const std::string expectedMessages[] = { "trace message", "debug message", "info message", "warn message",
                                            "error message" };

  for (std::size_t i = 0; i < 5; ++i)
  {
    EXPECT_EQ(entries[i].level, expectedLevels[i]);
    EXPECT_EQ(entries[i].category, LogCategory::script);
    EXPECT_EQ(entries[i].message, expectedMessages[i]);
  }
}

TEST_F(LogBindingsTest, OutOfRangeLevelFallsBackToInfo)
{
  const LogBindings bindings = LogBindingsProvider::getBindings();

  bindings.write(-1, "negative");
  bindings.write(99, "too high");

  const auto entries = m_sink->snapshot();
  ASSERT_EQ(entries.size(), 2u);

  EXPECT_EQ(entries[0].level, LogLevel::info);
  EXPECT_EQ(entries[0].category, LogCategory::script);
  EXPECT_EQ(entries[0].message, "negative");

  EXPECT_EQ(entries[1].level, LogLevel::info);
  EXPECT_EQ(entries[1].message, "too high");
}

TEST_F(LogBindingsTest, NullMessageProducesNoEntry)
{
  const LogBindings bindings = LogBindingsProvider::getBindings();

  bindings.write(2, nullptr);
  EXPECT_TRUE(m_sink->snapshot().empty());

  // Positive control: a non-null write right after is delivered, so the empty result above reflects the
  // null guard rather than some other break in the pipeline.
  bindings.write(2, "still works");
  ASSERT_EQ(m_sink->snapshot().size(), 1u);
  EXPECT_EQ(m_sink->snapshot()[0].message, "still works");
}

TEST_F(LogBindingsTest, Utf8MultibyteMessageIsDeliveredByteForByte)
{
  const LogBindings bindings = LogBindingsProvider::getBindings();

  const char* message = "caf\xC3\xA9";
  bindings.write(2, message);

  const auto entries = m_sink->snapshot();
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].message, std::string(message));
}
