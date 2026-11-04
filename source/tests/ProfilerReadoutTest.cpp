#include <gtest/gtest.h>

#include <ProfilerReadout.h>

#include <History.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace {
  profile::Sample sample(const uint64_t sequence, const int64_t startNanos, const uint32_t wallMicros,
                         const int64_t ticks = 0)
  {
    profile::Sample result;
    result.sequence = sequence;
    result.startNanos = startNanos;
    result.wallMicros = wallMicros;
    result.counters[0] = ticks;
    return result;
  }

  // Four 8 ms frames back to back after an old one that the one-second window must leave out.
  profile::History editorHistory()
  {
    profile::History history(16);
    history.push(sample(1, 0, 90000));

    for (uint64_t i = 0; i < 4; ++i)
    {
      history.push(sample(2 + i, 10'000'000'000 + static_cast<int64_t>(i) * 8'000'000, 8000));
    }

    return history;
  }

  // One old sample holding 50 ticks, then four recent samples of one tick each.
  profile::History serverHistory()
  {
    profile::History history(16);
    history.push(sample(1, 0, 1000, 50));

    for (uint64_t i = 0; i < 4; ++i)
    {
      history.push(sample(2 + i, 10'000'000'000 + static_cast<int64_t>(i) * 20'000'000, 2100, 1));
    }

    return history;
  }

  ServerLaneInput liveServer(const profile::History& history)
  {
    ServerLaneInput server;
    server.state = ServerLaneState::live;
    server.history = &history;
    server.ticksCounter = 0;
    server.tickMicros = {9000, 9000, 9000, 2100, 2100, 2100, 2100};
    server.sent.bytesPerSecond = 61.0 * 1024.0;
    return server;
  }

  ServerLaneInput stateOnly(const ServerLaneState state)
  {
    ServerLaneInput server;
    server.state = state;
    server.sent.bytesPerSecond = 5000.0;
    return server;
  }
}

TEST(ProfilerReadout, ALiveServerGivesAllThreeLines)
{
  const auto history = serverHistory();
  const auto lines = profilerReadout::lines(editorHistory(), liveServer(history));

  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[0], "8.0 ms  125 fps");
  EXPECT_EQ(lines[1], "srv 2.1 ms  (p95 2.1)");
  EXPECT_EQ(lines[2], "net 61.0 KB/s");
}

TEST(ProfilerReadout, EachNonLiveStateReplacesTheServerLineAndDropsTheRate)
{
  const profile::History editor = editorHistory();

  const auto waiting = profilerReadout::lines(editor, stateOnly(ServerLaneState::waiting));
  const auto unsupported = profilerReadout::lines(editor, stateOnly(ServerLaneState::unsupported));
  const auto disconnected = profilerReadout::lines(editor, stateOnly(ServerLaneState::disconnected));

  ASSERT_EQ(waiting.size(), 2u);
  ASSERT_EQ(unsupported.size(), 2u);
  ASSERT_EQ(disconnected.size(), 2u);
  EXPECT_EQ(waiting[1], "srv waiting");
  EXPECT_EQ(unsupported[1], "srv n/a");
  EXPECT_EQ(disconnected[1], "srv disconnected");
  EXPECT_EQ(waiting[0], "8.0 ms  125 fps");
}

TEST(ProfilerReadout, ALiveServerWithoutHistoryReadsZeroTicks)
{
  ServerLaneInput server = stateOnly(ServerLaneState::live);
  server.tickMicros = {5000, 5000};

  const auto lines = profilerReadout::lines(editorHistory(), server);

  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[1], "srv 0.0 ms  (p95 0.0)");
  EXPECT_EQ(lines[2], "net 4.9 KB/s");
}

TEST(ProfilerReadout, AnEmptyEditorHistoryReadsZero)
{
  const auto lines = profilerReadout::lines(profile::History(4), stateOnly(ServerLaneState::waiting));

  EXPECT_EQ(lines[0], "0.0 ms  0 fps");
}

TEST(ProfilerReadout, TenthsRoundAndNegativeOrNotANumberReadAsZero)
{
  EXPECT_EQ(profilerReadout::formatTenths(2.04), "2.0");
  EXPECT_EQ(profilerReadout::formatTenths(2.06), "2.1");
  EXPECT_EQ(profilerReadout::formatTenths(12.0), "12.0");
  EXPECT_EQ(profilerReadout::formatTenths(-3.0), "0.0");
  EXPECT_EQ(profilerReadout::formatTenths(std::numeric_limits<double>::quiet_NaN()), "0.0");
}
