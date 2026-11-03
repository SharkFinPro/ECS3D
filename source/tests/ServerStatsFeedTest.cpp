#include <gtest/gtest.h>

#include "Lane.h"
#include "ProfileScope.h"
#include "ServerStatsFeed.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace std::chrono_literals;

namespace {
  constexpr auto deltaIndex = static_cast<std::size_t>(net::MessageType::stateDelta);
  constexpr auto snapshotIndex = static_cast<std::size_t>(net::MessageType::snapshot);
  constexpr auto inputIndex = static_cast<std::size_t>(net::MessageType::inputState);

  class FeedRig {
  public:
    explicit FeedRig(const std::size_t historyCapacity = 16, const std::size_t tickRing = 8)
      : lane("server", historyCapacity, [this] { return now; })
    {
      lane.trackScope(profile::internName("Server tick"), tickRing);
      counters.ticks = lane.counterId("ticks");
      counters.droppedSteps = lane.counterId("droppedSteps");
      counters.objects = lane.counterId("objects");
      lane.bindToThisThread();
    }

    ~FeedRig() { profile::Lane::unbindThisThread(); }

    void advance(const profile::Clock::duration duration) { now += duration; }

    void timed(const char* name, const profile::Clock::duration duration)
    {
      const profile::Scope scope(profile::internName(name));
      advance(duration);
    }

    // One closed sample holding a "Server tick" scope with the given children, then one more millisecond of
    // tick time that no child owns.
    void tickSample(const int ticks, const int dropped, const int objects, const profile::Clock::duration scripts,
                    const profile::Clock::duration physics)
    {
      lane.beginSample();
      lane.addCounter(counters.ticks, ticks);
      lane.addCounter(counters.droppedSteps, dropped);
      lane.addCounter(counters.objects, objects);
      {
        const profile::Scope tick(profile::internName("Server tick"));
        if (scripts > 0ms)
        {
          timed("Scripts", scripts);
        }

        if (physics > 0ms)
        {
          timed("Physics", physics);
        }

        advance(1ms);
      }
      lane.endSample(true);
      advance(1ms);
    }

    [[nodiscard]] StatsFeedInput input() const
    {
      StatsFeedInput in;
      in.net = netSample;
      in.net.snapshots = &snapshotLog;
      in.editors = editors;
      in.flags = flags;
      in.now = wallNow;
      return in;
    }

    [[nodiscard]] net::ServerStats collect()
    {
      return collectServerStats(lane, counters, cursor, input());
    }

    profile::Clock::time_point now = profile::Clock::time_point{} + 1s;
    profile::Lane lane;
    StatsCounters counters;
    StatsFeedCursor cursor;
    NetStatsSample netSample;
    net::SnapshotLog snapshotLog;
    int64_t editors = 0;
    uint8_t flags = 0;
    std::chrono::steady_clock::time_point wallNow = std::chrono::steady_clock::time_point{} + 100s;
  };

  [[nodiscard]] net::SnapshotRecord snapshotAt(const std::chrono::steady_clock::time_point time,
                                               const net::SnapshotReason reason, const uint32_t bytes)
  {
    return {reason, bytes, 700, 2, time};
  }
}

TEST(ServerStatsFeed, FirstCollectCarriesSamplesWithOnlyTheNodesThatRan)
{
  FeedRig rig;
  rig.tickSample(1, 1, 5, 1ms, 2ms);
  rig.tickSample(2, 0, 7, 3ms, 0ms);

  const auto stats = rig.collect();
  const auto& tree = rig.lane.tree();
  const auto serverTick = *findStatsNode(tree, 0, "Server tick");
  const auto scripts = *findStatsNode(tree, serverTick, "Scripts");
  const auto physics = *findStatsNode(tree, serverTick, "Physics");

  ASSERT_EQ(stats.samples.size(), 2u);
  EXPECT_EQ(stats.samples[0].ticks, 1);
  EXPECT_EQ(stats.samples[0].droppedSteps, 1);
  EXPECT_EQ(stats.samples[0].wallMicros, 4000u);
  ASSERT_EQ(stats.samples[0].nodes.size(), 3u);
  EXPECT_EQ(stats.samples[0].nodes[0].node, serverTick);
  EXPECT_EQ(stats.samples[0].nodes[0].micros, 4000u);
  EXPECT_EQ(stats.samples[0].nodes[0].calls, 1);
  EXPECT_EQ(stats.samples[0].nodes[1].node, scripts);
  EXPECT_EQ(stats.samples[0].nodes[1].micros, 1000u);
  EXPECT_EQ(stats.samples[0].nodes[2].node, physics);
  EXPECT_EQ(stats.samples[0].nodes[2].micros, 2000u);

  EXPECT_EQ(stats.samples[1].ticks, 2);
  EXPECT_EQ(stats.samples[1].droppedSteps, 0);
  ASSERT_EQ(stats.samples[1].nodes.size(), 2u);
  EXPECT_EQ(stats.samples[1].nodes[0].node, serverTick);
  EXPECT_EQ(stats.samples[1].nodes[1].node, scripts);
  EXPECT_EQ(stats.samples[1].nodes[1].micros, 3000u);
}

TEST(ServerStatsFeed, FirstCollectCarriesTheTickDurationsTheEpochAndTheNodeTable)
{
  FeedRig rig;
  rig.tickSample(1, 0, 5, 1ms, 2ms);
  rig.tickSample(1, 0, 5, 3ms, 0ms);
  const auto stats = rig.collect();
  const auto& tree = rig.lane.tree();
  const auto serverTick = *findStatsNode(tree, 0, "Server tick");
  const auto scripts = *findStatsNode(tree, serverTick, "Scripts");

  EXPECT_EQ(stats.tickMicros, (std::vector<uint32_t>{4000, 4000}));
  EXPECT_EQ(stats.nodeTableEpoch, tree.size());
  ASSERT_EQ(stats.nodes.size(), tree.size() - 1);
  EXPECT_EQ(stats.nodes[serverTick - 1].name, "Server tick");
  EXPECT_EQ(stats.nodes[serverTick - 1].parent, 0);
  EXPECT_EQ(stats.nodes[scripts - 1].name, "Scripts");
  EXPECT_EQ(stats.nodes[scripts - 1].parent, serverTick);
  EXPECT_EQ(stats.objects, 5u);
}

TEST(ServerStatsFeed, ASecondCollectWithNothingNewCarriesNoSamplesTicksTrafficOrSnapshots)
{
  FeedRig rig;
  rig.tickSample(1, 0, 5, 1ms, 0ms);
  rig.netSample.out.messages[deltaIndex] = 3;
  rig.netSample.out.bytes[deltaIndex] = 300;
  rig.netSample.in.messages[inputIndex] = 1;
  rig.netSample.in.bytes[inputIndex] = 40;
  rig.snapshotLog.record(snapshotAt(rig.wallNow, net::SnapshotReason::join, 900));

  const auto first = rig.collect();
  EXPECT_EQ(first.samples.size(), 1u);
  EXPECT_EQ(first.tickMicros.size(), 1u);
  EXPECT_EQ(first.out.size(), 1u);
  EXPECT_EQ(first.in.size(), 1u);
  EXPECT_EQ(first.snapshots.size(), 1u);

  const auto second = rig.collect();
  EXPECT_TRUE(second.samples.empty());
  EXPECT_TRUE(second.tickMicros.empty());
  EXPECT_TRUE(second.out.empty());
  EXPECT_TRUE(second.in.empty());
  EXPECT_TRUE(second.snapshots.empty());
  EXPECT_EQ(second.nodes.size(), first.nodes.size());
  EXPECT_EQ(second.nodeTableEpoch, first.nodeTableEpoch);
}

TEST(ServerStatsFeed, OnlyTheTrafficTypesThatChangedAppearWithTheirDifferences)
{
  FeedRig rig;
  rig.netSample.out.messages[deltaIndex] = 10;
  rig.netSample.out.bytes[deltaIndex] = 1000;
  rig.netSample.out.messages[snapshotIndex] = 1;
  rig.netSample.out.bytes[snapshotIndex] = 5000;
  rig.netSample.in.messages[inputIndex] = 4;
  rig.netSample.in.bytes[inputIndex] = 80;
  static_cast<void>(rig.collect());

  rig.netSample.out.messages[deltaIndex] += 6;
  rig.netSample.out.bytes[deltaIndex] += 600;
  const auto stats = rig.collect();

  ASSERT_EQ(stats.out.size(), 1u);
  EXPECT_EQ(stats.out[0].type, static_cast<uint8_t>(net::MessageType::stateDelta));
  EXPECT_EQ(stats.out[0].messages, 6u);
  EXPECT_EQ(stats.out[0].bytes, 600u);
  EXPECT_TRUE(stats.in.empty());
}

TEST(ServerStatsFeed, TrafficListsTheFirstCollectInTypeOrderAndClampsTheMessageCount)
{
  FeedRig rig;
  rig.netSample.out.messages[snapshotIndex] = 5'000'000'000ull;
  rig.netSample.out.bytes[snapshotIndex] = 7;
  rig.netSample.out.messages[deltaIndex] = 2;
  rig.netSample.out.bytes[deltaIndex] = 0;

  const auto stats = rig.collect();

  ASSERT_EQ(stats.out.size(), 2u);
  EXPECT_EQ(stats.out[0].type, static_cast<uint8_t>(net::MessageType::snapshot));
  EXPECT_EQ(stats.out[0].messages, std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(stats.out[0].bytes, 7u);
  EXPECT_EQ(stats.out[1].type, static_cast<uint8_t>(net::MessageType::stateDelta));
  EXPECT_EQ(stats.out[1].messages, 2u);
}

TEST(ServerStatsFeed, SnapshotAgesComeFromTheTimeNowAndOnlyNewRecordsFollowTheCursor)
{
  FeedRig rig;
  const auto t0 = rig.wallNow;
  rig.snapshotLog.record(snapshotAt(t0 - 5s, net::SnapshotReason::join, 4096));
  rig.snapshotLog.record(snapshotAt(t0 - 400ms, net::SnapshotReason::sceneEdit, 8192));

  const auto first = rig.collect();

  ASSERT_EQ(first.snapshots.size(), 2u);
  EXPECT_EQ(first.snapshots[0].ageMillis, 5000u);
  EXPECT_EQ(first.snapshots[0].reason, net::SnapshotReason::join);
  EXPECT_EQ(first.snapshots[0].bytes, 4096u);
  EXPECT_EQ(first.snapshots[0].buildMicros, 700u);
  EXPECT_EQ(first.snapshots[0].recipients, 2);
  EXPECT_EQ(first.snapshots[1].ageMillis, 400u);
  EXPECT_EQ(first.snapshots[1].reason, net::SnapshotReason::sceneEdit);

  rig.wallNow += 1s;
  rig.snapshotLog.record(snapshotAt(rig.wallNow - 100ms, net::SnapshotReason::assetChange, 16));
  const auto second = rig.collect();

  ASSERT_EQ(second.snapshots.size(), 1u);
  EXPECT_EQ(second.snapshots[0].ageMillis, 100u);
  EXPECT_EQ(second.snapshots[0].reason, net::SnapshotReason::assetChange);
}

TEST(ServerStatsFeed, ARecordFromTheFutureHasAgeZero)
{
  FeedRig rig;
  rig.snapshotLog.record(snapshotAt(rig.wallNow + 3s, net::SnapshotReason::other, 1));

  const auto stats = rig.collect();

  ASSERT_EQ(stats.snapshots.size(), 1u);
  EXPECT_EQ(stats.snapshots[0].ageMillis, 0u);
}

TEST(ServerStatsFeed, SkipAdvancesEveryCursorSoTheNextCollectCarriesOnlyWhatFollows)
{
  FeedRig rig;
  rig.tickSample(1, 0, 5, 1ms, 0ms);
  rig.netSample.out.messages[deltaIndex] = 4;
  rig.netSample.out.bytes[deltaIndex] = 400;
  rig.netSample.in.messages[inputIndex] = 2;
  rig.netSample.in.bytes[inputIndex] = 20;
  rig.snapshotLog.record(snapshotAt(rig.wallNow, net::SnapshotReason::join, 9));

  skipServerStats(rig.lane, rig.cursor, rig.input());

  rig.tickSample(3, 0, 6, 2ms, 0ms);
  rig.netSample.out.messages[deltaIndex] = 5;
  rig.netSample.out.bytes[deltaIndex] = 450;
  rig.netSample.in.messages[inputIndex] = 3;
  rig.netSample.in.bytes[inputIndex] = 25;
  rig.snapshotLog.record(snapshotAt(rig.wallNow, net::SnapshotReason::sceneControl, 10));

  const auto stats = rig.collect();

  ASSERT_EQ(stats.samples.size(), 1u);
  EXPECT_EQ(stats.samples[0].ticks, 3);
  EXPECT_EQ(stats.tickMicros, (std::vector<uint32_t>{3000}));
  ASSERT_EQ(stats.out.size(), 1u);
  EXPECT_EQ(stats.out[0].messages, 1u);
  EXPECT_EQ(stats.out[0].bytes, 50u);
  ASSERT_EQ(stats.in.size(), 1u);
  EXPECT_EQ(stats.in[0].bytes, 5u);
  ASSERT_EQ(stats.snapshots.size(), 1u);
  EXPECT_EQ(stats.snapshots[0].reason, net::SnapshotReason::sceneControl);
}

TEST(ServerStatsFeed, TheTickAndDroppedCountersClampToAByte)
{
  FeedRig rig;
  rig.tickSample(300, 1000, 5, 1ms, 0ms);
  rig.tickSample(255, 256, 5, 1ms, 0ms);
  rig.tickSample(254, 0, 5, 1ms, 0ms);

  const auto stats = rig.collect();

  ASSERT_EQ(stats.samples.size(), 3u);
  EXPECT_EQ(stats.samples[0].ticks, 255);
  EXPECT_EQ(stats.samples[0].droppedSteps, 255);
  EXPECT_EQ(stats.samples[1].ticks, 255);
  EXPECT_EQ(stats.samples[1].droppedSteps, 255);
  EXPECT_EQ(stats.samples[2].ticks, 254);
  EXPECT_EQ(stats.samples[2].droppedSteps, 0);
}

TEST(ServerStatsFeed, SamplesAreCappedToTheNewestOnesOldestFirst)
{
  FeedRig rig(600);
  for (int i = 1; i <= 600; ++i)
  {
    rig.lane.beginSample();
    rig.advance(std::chrono::microseconds(i));
    rig.lane.endSample(true);
  }

  const auto stats = rig.collect();

  ASSERT_EQ(stats.samples.size(), net::maxServerStatsSamples);
  EXPECT_EQ(stats.samples.front().wallMicros, 89u);
  EXPECT_EQ(stats.samples.back().wallMicros, 600u);
}

TEST(ServerStatsFeed, TickDurationsAreLimitedToWhatTheRingStillHoldsNewestLast)
{
  FeedRig rig(16, 4);
  for (int i = 1; i <= 6; ++i)
  {
    rig.tickSample(1, 0, 1, std::chrono::milliseconds(i), 0ms);
  }

  const auto stats = rig.collect();

  EXPECT_EQ(stats.tickMicros, (std::vector<uint32_t>{4000, 5000, 6000, 7000}));
}

TEST(ServerStatsFeed, ConnectionsEditorsObjectsAndFlagsPassThroughClamped)
{
  FeedRig rig;
  rig.tickSample(1, 0, 40, 1ms, 0ms);
  rig.tickSample(1, 0, 41, 1ms, 0ms);
  rig.netSample.connections = 70000;
  rig.editors = 2;
  rig.flags = net::serverStatsFlags::editMode | net::serverStatsFlags::sceneRunning;

  const auto stats = rig.collect();

  EXPECT_EQ(stats.connections, std::numeric_limits<uint16_t>::max());
  EXPECT_EQ(stats.editors, 2);
  EXPECT_EQ(stats.objects, 41u);
  EXPECT_EQ(stats.flags, net::serverStatsFlags::editMode | net::serverStatsFlags::sceneRunning);
  EXPECT_EQ(stats.flags & net::serverStatsFlags::nodeTable, 0);
}

TEST(ServerStatsFeed, ObjectsAreZeroUntilASampleHasClosed)
{
  FeedRig rig;
  rig.netSample.connections = 1;

  const auto empty = rig.collect();
  EXPECT_EQ(empty.objects, 0u);
  EXPECT_EQ(empty.connections, 1);

  rig.tickSample(1, 0, 12, 1ms, 0ms);
  EXPECT_EQ(rig.collect().objects, 12u);
}
