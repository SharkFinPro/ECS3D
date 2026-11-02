#include <gtest/gtest.h>

#include "Lane.h"
#include "ProfileScope.h"
#include "StatsSummary.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

using namespace std::chrono_literals;

namespace {
  class StatsRig {
  public:
    explicit StatsRig(const std::size_t tickRing = 8)
      : lane("server", 16, [this] { return now; })
    {
      lane.trackScope(profile::internName("Server tick"), tickRing);
      counters.ticks = lane.counterId("ticks");
      counters.droppedSteps = lane.counterId("droppedSteps");
      counters.objects = lane.counterId("objects");
      lane.bindToThisThread();
    }

    ~StatsRig() { profile::Lane::unbindThisThread(); }

    void advance(const profile::Clock::duration duration) { now += duration; }

    // Mirrors what ServerApp does each iteration: the sample counter and the running total.
    void addTicks(const int count)
    {
      lane.addCounter(counters.ticks, count);
      cursor.ticks += static_cast<uint64_t>(count);
    }

    void addDropped(const int count)
    {
      lane.addCounter(counters.droppedSteps, count);
      cursor.droppedSteps += static_cast<uint64_t>(count);
    }

    void closeSampleOf(const profile::Clock::duration duration)
    {
      lane.beginSample();
      advance(duration);
      lane.endSample(true);
    }

    void timed(const char* name, const profile::Clock::duration duration)
    {
      const profile::Scope scope(profile::internName(name));
      advance(duration);
    }

    profile::Clock::time_point now = profile::Clock::time_point{} + 1s;
    profile::Lane lane;
    StatsCounters counters;
    StatsCursor cursor;
  };
}

TEST(StatsSummary, FormatsTheLineExactly)
{
  StatsInput in;
  in.coveredSeconds = 10.0;
  in.tickAvgMicros = 2100;
  in.tickP50Micros = 2000;
  in.tickP95Micros = 3000;
  in.tickP99Micros = 4400;
  in.tickMaxMicros = 9800;
  in.ticks = 500;
  in.droppedSteps = 0;
  in.scriptsMicros = 1200;
  in.physicsMicros = 200;
  in.collisionsMicros = 500;
  in.replicationMicros = 200;
  in.objects = 120;

  EXPECT_EQ(formatStatsLine(in),
            "stats 10.0s: tick avg 2.10 p50 2.00 p95 3.00 p99 4.40 max 9.80 ms | ticks 500 | dropped steps 0 | "
            "per tick period: scripts 1.20 physics 0.20 collisions 0.50 replication 0.20 ms | objects 120 | "
            "sent 0 B recv 0 B | conns 0 | snapshots 0");
}

TEST(StatsSummary, TheLineEndsWithTrafficConnectionsAndTheLargestSnapshot)
{
  StatsInput in;
  in.objects = 85;
  in.sentBytes = 626688;
  in.recvBytes = 31744;
  in.topSent = {StatsInput::TypeBytes{net::MessageType::stateDelta, 612352},
                StatsInput::TypeBytes{net::MessageType::serverLog, 12288}};
  in.connections = 2;
  in.snapshots = 1;
  in.largestSnapshot = {net::SnapshotReason::sceneEdit, 422400, 900, 2};

  const auto line = formatStatsLine(in);

  EXPECT_NE(line.find("objects 85 | sent 612.0 KB (stateDelta 598.0 KB, serverLog 12.0 KB) recv 31.0 KB | conns 2 | "
                      "snapshots 1 (largest 412.5 KB, sceneEdit)"),
            std::string::npos) << line;
}

TEST(StatsSummary, ByteSizesSwitchUnitAtTheBoundaries)
{
  EXPECT_EQ(statsDetail::byteSize(0), "0 B");
  EXPECT_EQ(statsDetail::byteSize(1023), "1023 B");
  EXPECT_EQ(statsDetail::byteSize(1024), "1.0 KB");
  EXPECT_EQ(statsDetail::byteSize(1536), "1.5 KB");
  EXPECT_EQ(statsDetail::byteSize(1048575), "1023.9 KB");
  EXPECT_EQ(statsDetail::byteSize(1048576), "1.0 MB");
  EXPECT_EQ(statsDetail::byteSize(5 * 1048576 + 524288), "5.5 MB");
}

TEST(StatsSummary, NoParenthesisWhenNothingWasSentAndOneEntryWhenOneTypeWas)
{
  StatsInput none;
  EXPECT_NE(formatStatsLine(none).find("| sent 0 B recv"), std::string::npos);

  StatsInput one;
  one.sentBytes = 2048;
  one.topSent[0] = {net::MessageType::snapshot, 2048};
  const auto line = formatStatsLine(one);

  EXPECT_NE(line.find("sent 2.0 KB (snapshot 2.0 KB) recv"), std::string::npos) << line;
  EXPECT_EQ(line.find(", "), std::string::npos) << line;
}

TEST(StatsSummary, GatherTakesTrafficAsTheChangeSinceThePreviousSummary)
{
  StatsRig rig;
  NetStatsSample sample;
  sample.out.bytes[static_cast<std::size_t>(net::MessageType::stateDelta)] = 5000;
  sample.out.bytes[static_cast<std::size_t>(net::MessageType::serverLog)] = 300;
  sample.out.bytes[static_cast<std::size_t>(net::MessageType::snapshot)] = 100;
  sample.in.bytes[static_cast<std::size_t>(net::MessageType::inputState)] = 70;
  sample.connections = 2;

  const auto first = gatherStats(rig.lane, rig.counters, rig.cursor, sample);

  sample.out.bytes[static_cast<std::size_t>(net::MessageType::stateDelta)] += 1000;
  sample.in.bytes[static_cast<std::size_t>(net::MessageType::inputState)] += 30;
  sample.connections = 1;
  const auto second = gatherStats(rig.lane, rig.counters, rig.cursor, sample);

  EXPECT_EQ(first.sentBytes, 5400u);
  EXPECT_EQ(first.recvBytes, 70u);
  EXPECT_EQ(first.topSent[0].type, net::MessageType::stateDelta);
  EXPECT_EQ(first.topSent[0].bytes, 5000u);
  EXPECT_EQ(first.topSent[1].type, net::MessageType::serverLog);
  EXPECT_EQ(first.topSent[1].bytes, 300u);
  EXPECT_EQ(first.connections, 2);
  EXPECT_EQ(second.sentBytes, 1000u);
  EXPECT_EQ(second.recvBytes, 30u);
  EXPECT_EQ(second.topSent[0].bytes, 1000u);
  EXPECT_EQ(second.topSent[1].bytes, 0u);
  EXPECT_EQ(second.connections, 1);
}

TEST(StatsSummary, GatherNamesTheLargestSnapshotRecordedSincePreviousSummary)
{
  StatsRig rig;
  net::SnapshotLog log;
  NetStatsSample sample;
  sample.snapshots = &log;

  log.record({net::SnapshotReason::join, 9000, 1, 1});
  const auto first = gatherStats(rig.lane, rig.counters, rig.cursor, sample);

  log.record({net::SnapshotReason::sceneControl, 100, 1, 2});
  log.record({net::SnapshotReason::assetChange, 400, 1, 2});
  log.record({net::SnapshotReason::sceneEdit, 200, 1, 2});
  const auto second = gatherStats(rig.lane, rig.counters, rig.cursor, sample);
  const auto third = gatherStats(rig.lane, rig.counters, rig.cursor, sample);

  EXPECT_EQ(first.snapshots, 1);
  EXPECT_EQ(first.largestSnapshot.reason, net::SnapshotReason::join);
  EXPECT_EQ(second.snapshots, 3);
  EXPECT_EQ(second.largestSnapshot.bytes, 400u);
  EXPECT_EQ(second.largestSnapshot.reason, net::SnapshotReason::assetChange);
  EXPECT_EQ(third.snapshots, 0);
  EXPECT_NE(formatStatsLine(second).find("snapshots 3 (largest 400 B, assetChange)"), std::string::npos);
  EXPECT_NE(formatStatsLine(third).find("snapshots 0"), std::string::npos);
}

TEST(StatsSummary, RoundsToTheNearestHundredthAndPadsTheFraction)
{
  StatsInput in;
  in.tickAvgMicros = 1239;
  in.tickP50Micros = 4;
  in.tickP95Micros = 12000;

  const auto line = formatStatsLine(in);

  EXPECT_NE(line.find("tick avg 1.24 p50 0.00 p95 12.00"), std::string::npos) << line;
}

TEST(StatsSummary, TheIdleLineIsFixed)
{
  EXPECT_EQ(idleStatsLine(), "stats: idle (no ticks)");
}

TEST(StatsSummary, GatherSummarizesSamplesTicksAndNodes)
{
  StatsRig rig;

  rig.lane.beginSample();
  rig.addTicks(1);
  rig.addDropped(1);
  rig.lane.addCounter(rig.counters.objects, 5);
  {
    const profile::Scope tick(profile::internName("Server tick"));
    rig.timed("Scripts", 1ms);
    rig.timed("Physics", 2ms);
    rig.advance(1ms);
  }
  rig.timed("Replication", 1ms);
  rig.lane.endSample(true);

  rig.advance(5ms);

  rig.lane.beginSample();
  rig.addTicks(1);
  rig.addDropped(2);
  rig.lane.addCounter(rig.counters.objects, 7);
  {
    const profile::Scope tick(profile::internName("Server tick"));
    rig.timed("Scripts", 3ms);
    rig.timed("Physics", 2ms);
    rig.timed("Collisions", 1ms);
    rig.advance(2ms);
  }
  rig.lane.endSample(true);

  const auto in = gatherStats(rig.lane, rig.counters, rig.cursor);

  EXPECT_NEAR(in.coveredSeconds, 0.018, 1e-9);
  EXPECT_EQ(in.ticks, 2);
  EXPECT_EQ(in.droppedSteps, 3);
  EXPECT_EQ(in.objects, 7);
  EXPECT_NEAR(in.tickAvgMicros, 6000, 1e-6);
  EXPECT_NEAR(in.tickP50Micros, 4000, 1e-6);
  EXPECT_NEAR(in.tickP95Micros, 8000, 1e-6);
  EXPECT_NEAR(in.tickP99Micros, 8000, 1e-6);
  EXPECT_NEAR(in.tickMaxMicros, 8000, 1e-6);
  EXPECT_NEAR(in.scriptsMicros, 2000, 1e-6);
  EXPECT_NEAR(in.physicsMicros, 2000, 1e-6);
  EXPECT_NEAR(in.collisionsMicros, 500, 1e-6);
  EXPECT_NEAR(in.replicationMicros, 500, 1e-6);
}

TEST(StatsSummary, GatherCoversOnlyWhatFollowsThePreviousSummary)
{
  StatsRig rig;

  rig.lane.beginSample();
  rig.addTicks(3);
  {
    const profile::Scope tick(profile::internName("Server tick"));
    rig.advance(10ms);
  }
  rig.lane.endSample(true);
  const auto first = gatherStats(rig.lane, rig.counters, rig.cursor);

  rig.lane.beginSample();
  rig.addTicks(1);
  {
    const profile::Scope tick(profile::internName("Server tick"));
    rig.advance(2ms);
  }
  rig.lane.endSample(true);
  const auto second = gatherStats(rig.lane, rig.counters, rig.cursor);

  EXPECT_EQ(first.ticks, 3);
  EXPECT_NEAR(first.tickMaxMicros, 10000, 1e-6);
  EXPECT_EQ(second.ticks, 1);
  EXPECT_NEAR(second.tickMaxMicros, 2000, 1e-6);
}

TEST(StatsSummary, GatherReportsNoTicksWhenSamplesClosedWithoutOne)
{
  StatsRig rig;

  rig.lane.beginSample();
  rig.advance(20ms);
  rig.lane.endSample(true);

  const auto in = gatherStats(rig.lane, rig.counters, rig.cursor);

  EXPECT_EQ(in.ticks, 0);
  EXPECT_NEAR(in.coveredSeconds, 0.02, 1e-9);
  EXPECT_DOUBLE_EQ(in.tickMaxMicros, 0.0);
}

TEST(StatsSummary, GatherWithNothingRecordedIsAllZero)
{
  StatsRig rig;

  const auto in = gatherStats(rig.lane, rig.counters, rig.cursor);

  EXPECT_EQ(in.ticks, 0);
  EXPECT_DOUBLE_EQ(in.coveredSeconds, 0.0);
  EXPECT_DOUBLE_EQ(in.tickAvgMicros, 0.0);
}

TEST(StatsSummary, ANodeThatNeverAppearedPrintsZero)
{
  StatsRig rig;

  rig.lane.beginSample();
  rig.addTicks(1);
  {
    const profile::Scope tick(profile::internName("Server tick"));
    rig.timed("Physics", 4ms);
  }
  rig.lane.endSample(true);

  const auto in = gatherStats(rig.lane, rig.counters, rig.cursor);

  EXPECT_NEAR(in.physicsMicros, 4000, 1e-6);
  EXPECT_DOUBLE_EQ(in.scriptsMicros, 0.0);
  EXPECT_DOUBLE_EQ(in.collisionsMicros, 0.0);
  EXPECT_DOUBLE_EQ(in.replicationMicros, 0.0);
  EXPECT_NE(formatStatsLine(in).find("scripts 0.00 physics 4.00 collisions 0.00 replication 0.00"), std::string::npos);
}

TEST(StatsSummary, GatherKeepsOnlyTheTickDurationsTheRingStillHolds)
{
  StatsRig rig(2);

  rig.lane.beginSample();
  rig.addTicks(3);
  for (const auto duration : { 30ms, 2ms, 4ms })
  {
    const profile::Scope tick(profile::internName("Server tick"));
    rig.advance(duration);
  }
  rig.lane.endSample(true);

  const auto in = gatherStats(rig.lane, rig.counters, rig.cursor);

  EXPECT_EQ(in.ticks, 3);
  EXPECT_NEAR(in.tickMaxMicros, 4000, 1e-6);
  EXPECT_NEAR(in.tickAvgMicros, 3000, 1e-6);
}

TEST(StatsSummary, RoundsFromIntegerMicrosecondsAtACarryAndAHalf)
{
  StatsInput in;
  in.tickAvgMicros = 1005;
  in.tickP50Micros = 1999;
  in.tickP95Micros = 1004;
  in.tickP99Micros = 123456789;
  in.tickMaxMicros = 9999;

  const auto line = formatStatsLine(in);

  EXPECT_NE(line.find("tick avg 1.01 p50 2.00 p95 1.00 p99 123456.79 max 10.00"), std::string::npos) << line;
}

TEST(StatsSummary, AnAverageOfMicrosecondsRoundsOnTheHundredths)
{
  StatsInput in;
  in.scriptsMicros = 1004.9;
  in.physicsMicros = 1005.1;

  const auto line = formatStatsLine(in);

  EXPECT_NE(line.find("scripts 1.00 physics 1.01"), std::string::npos) << line;
}

TEST(StatsSummary, TicksAndDroppedStepsAreTheChangeInTheRunningTotals)
{
  StatsRig rig;

  rig.addTicks(7);
  rig.addDropped(2);
  const auto first = gatherStats(rig.lane, rig.counters, rig.cursor);
  rig.addTicks(5);
  const auto second = gatherStats(rig.lane, rig.counters, rig.cursor);

  EXPECT_EQ(first.ticks, 7);
  EXPECT_EQ(first.droppedSteps, 2);
  EXPECT_EQ(second.ticks, 5);
  EXPECT_EQ(second.droppedSteps, 0);
}

TEST(StatsSummary, ATickTotalBeyondTheHistoryIsNotUndercounted)
{
  StatsRig rig;

  for (int i = 0; i < 20; ++i)
  {
    rig.lane.beginSample();
    rig.addTicks(2);
    rig.advance(1ms);
    rig.lane.endSample(true);
  }

  const auto in = gatherStats(rig.lane, rig.counters, rig.cursor);

  EXPECT_EQ(rig.lane.history().size(), 16u);
  EXPECT_EQ(in.ticks, 40);
}

TEST(StatsSummary, AWrappedHistoryCoversWhatItHoldsThenOnlyTheNewSamples)
{
  StatsRig rig;

  for (int i = 0; i < 20; ++i)
  {
    rig.closeSampleOf(1ms);
    rig.advance(1ms);
  }

  const auto first = gatherStats(rig.lane, rig.counters, rig.cursor);

  for (int i = 0; i < 3; ++i)
  {
    rig.closeSampleOf(1ms);
    rig.advance(1ms);
  }

  const auto second = gatherStats(rig.lane, rig.counters, rig.cursor);

  // 16 samples of 1 ms spaced 2 ms apart span 31 ms; 3 span 5 ms.
  EXPECT_NEAR(first.coveredSeconds, 0.031, 1e-9);
  EXPECT_NEAR(second.coveredSeconds, 0.005, 1e-9);
}

TEST(StatsSummary, SamplesSinceCountsBySequenceNotByDistanceFromTheNewest)
{
  profile::History history(8);
  for (const uint64_t sequence : { 1u, 2u, 5u, 9u })
  {
    profile::Sample sample;
    sample.sequence = sequence;
    history.push(sample);
  }

  EXPECT_EQ(samplesSince(history, 0), 4u);
  EXPECT_EQ(samplesSince(history, 2), 2u);
  EXPECT_EQ(samplesSince(history, 5), 1u);
  EXPECT_EQ(samplesSince(history, 9), 0u);
}
