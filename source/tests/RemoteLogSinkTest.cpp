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

TEST(RemoteLogSink, TruncationBacksUpOffAUtf8ContinuationByteRatherThanSplittingIt)
{
  RemoteLogSink sink(10);

  // "e with acute" as UTF-8 is the two bytes 0xC3 0xA9. Placed so the lead byte (0xC3) lands exactly at
  // the cap and its continuation byte (0xA9) just past it, a plain byte-count truncation would cut
  // between them and leave a lone lead byte - not valid UTF-8, and not something writeString's pairing
  // read on the editor side should ever have to fail on.
  std::string message(RemoteLogSink::maxMessageBytes - 1, 'x');
  message += "\xC3\xA9";
  message += std::string(1000, 'y');
  ASSERT_GT(message.size(), RemoteLogSink::maxMessageBytes);

  sink.write(entry(message));

  const auto drained = sink.drain(10);

  ASSERT_EQ(drained.entries.size(), 1u);
  const auto& truncated = drained.entries[0].message;

  // The whole two-byte character is dropped rather than split: the kept text ends at the last 'x', one
  // byte short of the cap, not on the character's lead byte.
  const auto markerStart = truncated.find(" ... [truncated]");
  ASSERT_NE(markerStart, std::string::npos);
  EXPECT_EQ(markerStart, RemoteLogSink::maxMessageBytes - 1);
  EXPECT_EQ(truncated.substr(0, markerStart), std::string(RemoteLogSink::maxMessageBytes - 1, 'x'));
}

TEST(RemoteLogSink, TruncationKeepsACompleteMultiByteCharacterThatFitsExactly)
{
  RemoteLogSink sink(10);

  // Positive control for the previous test: when the multi-byte character fits entirely within the cap,
  // truncation must not back up past it unnecessarily.
  std::string message(RemoteLogSink::maxMessageBytes - 2, 'x');
  message += "\xC3\xA9";
  message += std::string(1000, 'y');
  ASSERT_GT(message.size(), RemoteLogSink::maxMessageBytes);

  sink.write(entry(message));

  const auto drained = sink.drain(10);

  ASSERT_EQ(drained.entries.size(), 1u);
  const auto& truncated = drained.entries[0].message;

  const auto markerStart = truncated.find(" ... [truncated]");
  ASSERT_NE(markerStart, std::string::npos);
  EXPECT_EQ(markerStart, RemoteLogSink::maxMessageBytes);
  EXPECT_EQ(truncated.substr(0, markerStart),
            std::string(RemoteLogSink::maxMessageBytes - 2, 'x') + "\xC3\xA9");
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

namespace {
  const std::string truncationMarkerText = " ... [truncated]";

  // True when every byte belongs to a complete, well-formed UTF-8 sequence of the given lengths.
  [[nodiscard]] bool isWholeUtf8(const std::string& text)
  {
    std::size_t i = 0;
    while (i < text.size())
    {
      const auto lead = static_cast<unsigned char>(text[i]);
      std::size_t length = 0;
      if ((lead & 0x80) == 0x00)
      {
        length = 1;
      }
      else if ((lead & 0xE0) == 0xC0)
      {
        length = 2;
      }
      else if ((lead & 0xF0) == 0xE0)
      {
        length = 3;
      }
      else if ((lead & 0xF8) == 0xF0)
      {
        length = 4;
      }
      else
      {
        return false;
      }

      if (i + length > text.size())
      {
        return false;
      }

      for (std::size_t j = 1; j < length; ++j)
      {
        if ((static_cast<unsigned char>(text[i + j]) & 0xC0) != 0x80)
        {
          return false;
        }
      }

      i += length;
    }

    return true;
  }

  // Writes a message of `prefix` filler bytes then `character` repeated past the cap, and returns what the
  // sink queued for it.
  [[nodiscard]] std::string truncatedMessage(const std::size_t prefix, const std::string& character)
  {
    std::string message(prefix, 'x');
    while (message.size() <= RemoteLogSink::maxMessageBytes + 8)
    {
      message += character;
    }

    RemoteLogSink sink(10);
    sink.write(entry(message));
    const auto drained = sink.drain(10);
    return drained.entries.at(0).message;
  }

  void expectCutsAtEveryOffsetStayWholeUtf8(const std::string& character)
  {
    // Shifting the prefix by one byte at a time moves the cap across every offset inside a character.
    const auto width = character.size();
    for (std::size_t shift = 0; shift < width; ++shift)
    {
      const auto prefix = RemoteLogSink::maxMessageBytes - width * 3 + shift;
      const auto stored = truncatedMessage(prefix, character);

      ASSERT_GE(stored.size(), truncationMarkerText.size()) << "width " << width << " shift " << shift;
      const auto markerStart = stored.size() - truncationMarkerText.size();
      EXPECT_EQ(stored.substr(markerStart), truncationMarkerText) << "width " << width << " shift " << shift;

      const auto kept = stored.substr(0, markerStart);
      EXPECT_TRUE(isWholeUtf8(kept)) << "width " << width << " shift " << shift;
      EXPECT_LE(kept.size(), RemoteLogSink::maxMessageBytes) << "width " << width << " shift " << shift;
      // Positive control: only the partial character is lost, never a whole extra character.
      EXPECT_GT(kept.size() + width, RemoteLogSink::maxMessageBytes) << "width " << width << " shift " << shift;
    }
  }
}

TEST(RemoteLogSink, ValidatorRejectsASplitSequence)
{
  // Positive control for the truncation tests below: the validator they lean on must fail a cut character.
  EXPECT_TRUE(isWholeUtf8("ab\xC3\xA9"));
  EXPECT_FALSE(isWholeUtf8("ab\xC3"));
  EXPECT_FALSE(isWholeUtf8("ab\xE2\x82"));
  EXPECT_FALSE(isWholeUtf8("ab\xF0\x9F\x98"));
}

TEST(RemoteLogSink, TwoByteCharactersCutAtTheCapStayWholeAtEveryOffset)
{
  expectCutsAtEveryOffsetStayWholeUtf8("\xC3\xA9");
}

TEST(RemoteLogSink, ThreeByteCharactersCutAtTheCapStayWholeAtEveryOffset)
{
  expectCutsAtEveryOffsetStayWholeUtf8("\xE2\x82\xAC");
}

TEST(RemoteLogSink, FourByteCharactersCutAtTheCapStayWholeAtEveryOffset)
{
  expectCutsAtEveryOffsetStayWholeUtf8("\xF0\x9F\x98\x80");
}

TEST(RemoteLogSink, AMessageOfThreeByteCharactersExactlyAtTheCapIsNotTruncated)
{
  RemoteLogSink sink(10);

  std::string message;
  while (message.size() + 3 <= RemoteLogSink::maxMessageBytes)
  {
    message += "\xE2\x82\xAC";
  }
  message += std::string(RemoteLogSink::maxMessageBytes - message.size(), 'x');
  ASSERT_EQ(message.size(), RemoteLogSink::maxMessageBytes);

  sink.write(entry(message));

  const auto drained = sink.drain(10);
  ASSERT_EQ(drained.entries.size(), 1u);
  EXPECT_EQ(drained.entries[0].message, message);
}

TEST(RemoteLogSink, ACutThatLandsOnAStrayContinuationByteDropsIt)
{
  RemoteLogSink sink(10);

  // 0xFF starts no UTF-8 sequence; the cut backs up to it and drops it instead of keeping invalid bytes.
  std::string message(RemoteLogSink::maxMessageBytes - 1, 'x');
  message += "\xFF";
  message += std::string(1000, 'y');

  sink.write(entry(message));

  const auto drained = sink.drain(10);
  ASSERT_EQ(drained.entries.size(), 1u);
  const auto& stored = drained.entries[0].message;
  const auto markerStart = stored.find(truncationMarkerText);
  ASSERT_NE(markerStart, std::string::npos);
  EXPECT_EQ(markerStart, RemoteLogSink::maxMessageBytes - 1);
}
