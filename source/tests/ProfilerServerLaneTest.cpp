#include <gtest/gtest.h>

#include <History.h>
#include <ProfilerServerLane.h>

#include <cstdint>
#include <limits>
#include <vector>

namespace {

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

TEST(ServerLaneFormat, ValuesRoundBeforeTheUnitIsChosen)
{
  EXPECT_EQ(profilerServerLane::formatRate(1023.96 * 1024.0), "1.0 MB/s");
  EXPECT_EQ(profilerServerLane::formatRate(1023.94 * 1024.0), "1023.9 KB/s");
  EXPECT_EQ(profilerServerLane::formatRate(1048575.0), "1.0 MB/s");
  EXPECT_EQ(profilerServerLane::formatRate(1023.6), "1.0 KB/s");
  EXPECT_EQ(profilerServerLane::formatRate(1023.4), "1023 B/s");
  EXPECT_EQ(profilerServerLane::formatSize(1048575), "1.0 MB");
}

TEST(ServerLaneFormat, ATrafficLineShowsBytesMessagesAndTypes)
{
  ServerTrafficDirection direction;
  direction.bytesPerSecond = 61.2 * 1024.0;
  direction.messagesPerSecond = 51.6;
  direction.types = {{"stateDelta", 0, 0.98}, {"serverLog", 0, 0.02}};

  EXPECT_EQ(profilerServerLane::describeTrafficLine("Server sent", direction),
            "Server sent     61.2 KB/s  52 msg/s  (stateDelta 98%, serverLog 2%)");
}

TEST(ServerLaneFormat, AnEmptyDirectionHasNoTypeList)
{
  EXPECT_EQ(profilerServerLane::describeTrafficLine("Editor sent", {}), "Editor sent     0 B/s  0 msg/s");
}

TEST(ServerLaneFormat, TheNetworkBlockHasServerAndEditorLinesInBothDirections)
{
  profile::History history(4);
  ServerLaneInput lane;
  lane.history = &history;
  lane.sent.bytesPerSecond = 2048.0;
  lane.sent.messagesPerSecond = 10.0;
  lane.editorReceived.bytesPerSecond = 1024.0;
  lane.editorReceived.messagesPerSecond = 4.0;
  lane.editorReceived.types = {{"stateDelta", 0, 1.0}};

  const auto lines = profilerServerLane::describeAllTraffic(lane);

  ASSERT_EQ(lines.size(), 4u);
  EXPECT_EQ(lines[0], "Server sent     2.0 KB/s  10 msg/s");
  EXPECT_EQ(lines[1], "Server received 0 B/s  0 msg/s");
  EXPECT_EQ(lines[2], "Editor sent     0 B/s  0 msg/s");
  EXPECT_EQ(lines[3], "Editor received 1.0 KB/s  4 msg/s  (stateDelta 100%)");
}

TEST(ServerLaneFormat, AServerThatHasNotReportedShowsOnlyTheEditorsLines)
{
  ServerLaneInput lane;
  lane.state = ServerLaneState::unsupported;
  lane.editorSent.bytesPerSecond = 1024.0;

  const auto lines = profilerServerLane::describeAllTraffic(lane);

  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0], "Editor sent     1.0 KB/s  0 msg/s");
  EXPECT_FALSE(profilerServerLane::hasServerFigures(lane));

  profile::History history(4);
  lane.history = &history;
  EXPECT_TRUE(profilerServerLane::hasServerFigures(lane));
  EXPECT_EQ(profilerServerLane::describeAllTraffic(lane).size(), 4u);
}
