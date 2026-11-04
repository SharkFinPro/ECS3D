#include <gtest/gtest.h>

#include <ProfilerServerLane.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <vector>

namespace {
  using namespace std::chrono_literals;
  using Meter = profilerServerLane::RateMeter;

  ServerLaneInput input(const ServerLaneState state, const bool sceneRunning = true)
  {
    ServerLaneInput lane;
    lane.state = state;
    lane.sceneRunning = sceneRunning;
    return lane;
  }
}

TEST(ServerLaneNotice, EachStateHasItsLine)
{
  EXPECT_EQ(serverLaneNotice(input(ServerLaneState::waiting)), "Waiting for server statistics...");
  EXPECT_EQ(serverLaneNotice(input(ServerLaneState::unsupported)), "This server does not report statistics.");
  EXPECT_EQ(serverLaneNotice(input(ServerLaneState::disconnected)), "Disconnected");
  EXPECT_EQ(serverLaneNotice(input(ServerLaneState::live, false)), "Scene stopped");
}

TEST(ServerLaneNotice, ALiveRunningSceneAndAnOffLaneHaveNoLine)
{
  EXPECT_EQ(serverLaneNotice(input(ServerLaneState::live, true)), "");
  EXPECT_EQ(serverLaneNotice(input(ServerLaneState::off, false)), "");
}

TEST(ServerLaneNotice, ADisconnectedLaneWinsOverAStoppedScene)
{
  EXPECT_EQ(serverLaneNotice(input(ServerLaneState::disconnected, false)), "Disconnected");
}

TEST(ServerLaneFormat, RatesUseOneDecimalAndSwitchUnitAtTheBoundaries)
{
  EXPECT_EQ(profilerServerLane::formatRate(0.0), "0 B/s");
  EXPECT_EQ(profilerServerLane::formatRate(1023.0), "1023 B/s");
  EXPECT_EQ(profilerServerLane::formatRate(1024.0), "1.0 KB/s");
  EXPECT_EQ(profilerServerLane::formatRate(61.2 * 1024.0), "61.2 KB/s");
  EXPECT_EQ(profilerServerLane::formatRate(1024.0 * 1024.0), "1.0 MB/s");
  EXPECT_EQ(profilerServerLane::formatRate(3.5 * 1024.0 * 1024.0 * 1024.0), "3.5 GB/s");
}

TEST(ServerLaneFormat, NegativeAndNotANumberRatesReadAsZero)
{
  EXPECT_EQ(profilerServerLane::formatRate(-5.0), "0 B/s");
  EXPECT_EQ(profilerServerLane::formatRate(std::numeric_limits<double>::quiet_NaN()), "0 B/s");
}

TEST(ServerLaneFormat, SizesDropTheRateSuffix)
{
  EXPECT_EQ(profilerServerLane::formatSize(512), "512 B");
  EXPECT_EQ(profilerServerLane::formatSize(12 * 1024 + 307), "12.3 KB");
}

TEST(ServerLaneFormat, TrafficListsTheLargestTypesWithRoundedShares)
{
  ServerTrafficDirection direction;
  direction.bytesPerSecond = 2048.0;
  direction.types = {{"stateDelta", 800, 0.8}, {"snapshot", 150, 0.15}, {"editStatus", 40, 0.04},
                     {"join", 10, 0.01}};

  EXPECT_EQ(profilerServerLane::describeTraffic(direction),
            "2.0 KB/s  (stateDelta 80%, snapshot 15%, editStatus 4%)");
  EXPECT_EQ(profilerServerLane::describeTraffic(direction, 1), "2.0 KB/s  (stateDelta 80%)");
  EXPECT_EQ(profilerServerLane::describeTraffic({}), "0 B/s");
}

TEST(ServerLaneFormat, SnapshotSummaryNamesTheNewestSnapshot)
{
  ServerLaneInput lane;
  lane.snapshotsLastMinute = 3;

  EXPECT_EQ(profilerServerLane::describeSnapshots(lane), "none yet  in last minute: 3");

  lane.snapshots.push_back({4.9, 2048, "sceneEdit", 1.5, 2});
  lane.largestSnapshotBytes = 90 * 1024;

  EXPECT_EQ(profilerServerLane::describeSnapshots(lane),
            "last 2.0 KB (sceneEdit, 4s ago)  largest 90.0 KB  in last minute: 3");
}

TEST(ServerLaneRateMeter, ReadsZeroUntilAnIntervalHasPassed)
{
  Meter meter;
  const Meter::Clock::time_point start{100s};

  EXPECT_DOUBLE_EQ(meter.update(1000, start), 0.0);
  EXPECT_DOUBLE_EQ(meter.update(2000, start + 100ms), 0.0);
}

TEST(ServerLaneRateMeter, DifferencesTheTotalOverTheElapsedTimeAndHoldsBetweenIntervals)
{
  Meter meter;
  const Meter::Clock::time_point start{100s};

  static_cast<void>(meter.update(1000, start));

  EXPECT_DOUBLE_EQ(meter.update(2000, start + 500ms), 2000.0);
  EXPECT_DOUBLE_EQ(meter.update(2100, start + 600ms), 2000.0);
  EXPECT_DOUBLE_EQ(meter.update(2600, start + 1000ms), 1200.0);
}

TEST(ServerLaneRateMeter, ATotalThatWentBackRestartsTheMeasurement)
{
  Meter meter;
  const Meter::Clock::time_point start{100s};

  static_cast<void>(meter.update(5000, start));
  ASSERT_GT(meter.update(9000, start + 1s), 0.0);

  EXPECT_DOUBLE_EQ(meter.update(10, start + 2s), 0.0);
  EXPECT_DOUBLE_EQ(meter.update(1010, start + 3s), 1000.0);
}

TEST(ServerLaneTicks, SummarizesOnlyTheNewestRequestedTicksPerTick)
{
  const std::vector<uint32_t> ticks{900000, 1000, 2000, 3000, 4000, 20000};

  const auto summary = profilerServerLane::summarizeTicks(ticks, 5);

  EXPECT_EQ(summary.ticks, 5u);
  EXPECT_DOUBLE_EQ(summary.averageMillis, 6.0);
  EXPECT_DOUBLE_EQ(summary.maxMillis, 20.0);
  EXPECT_DOUBLE_EQ(summary.p95Millis, 20.0);
}

TEST(ServerLaneTicks, ACountPastWhatWasKeptUsesEverythingAndNoneGivesZeros)
{
  const std::vector<uint32_t> ticks{1000, 3000};

  EXPECT_EQ(profilerServerLane::summarizeTicks(ticks, 50).ticks, 2u);
  EXPECT_DOUBLE_EQ(profilerServerLane::summarizeTicks(ticks, 50).averageMillis, 2.0);
  EXPECT_EQ(profilerServerLane::summarizeTicks(ticks, 0).ticks, 0u);
  EXPECT_DOUBLE_EQ(profilerServerLane::summarizeTicks({}, 3).maxMillis, 0.0);
}

TEST(ServerLaneTicks, WindowTickCountSumsTheTicksCounterAndCapsAtTheAvailableTicks)
{
  profile::History history(8);

  for (uint64_t i = 1; i <= 4; ++i)
  {
    profile::Sample sample;
    sample.sequence = i;
    sample.startNanos = static_cast<int64_t>(i) * 60000000;
    sample.wallMicros = 60000;
    sample.counters[0] = 3;
    history.push(sample);
  }

  EXPECT_EQ(profilerServerLane::windowTickCount(history, 2, 0, 100), 6u);
  EXPECT_EQ(profilerServerLane::windowTickCount(history, 4, 0, 100), 12u);
  EXPECT_EQ(profilerServerLane::windowTickCount(history, 4, 0, 5), 5u);
}
