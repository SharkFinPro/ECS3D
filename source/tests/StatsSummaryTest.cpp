#include <gtest/gtest.h>

#include "Lane.h"
#include "ProfileScope.h"
#include "StatsSummary.h"

#include <chrono>
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
  in.tickAvgMs = 2.1;
  in.tickP50Ms = 2.0;
  in.tickP95Ms = 3.0;
  in.tickP99Ms = 4.4;
  in.tickMaxMs = 9.8;
  in.ticks = 500;
  in.droppedSteps = 0;
  in.scriptsMs = 1.2;
  in.physicsMs = 0.2;
  in.collisionsMs = 0.5;
  in.replicationMs = 0.2;
  in.objects = 120;

  EXPECT_EQ(formatStatsLine(in),
            "stats 10.0s: tick avg 2.10 p50 2.00 p95 3.00 p99 4.40 max 9.80 ms | ticks 500 | dropped steps 0 | "
            "per tick period: scripts 1.20 physics 0.20 collisions 0.50 replication 0.20 ms | objects 120");
}

TEST(StatsSummary, RoundsToTheNearestHundredthAndPadsTheFraction)
{
  StatsInput in;
  in.tickAvgMs = 1.239;
  in.tickP50Ms = 0.004;
  in.tickP95Ms = 12.0;

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
  rig.lane.addCounter(rig.counters.ticks, 1);
  rig.lane.addCounter(rig.counters.droppedSteps, 1);
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
  rig.lane.addCounter(rig.counters.ticks, 1);
  rig.lane.addCounter(rig.counters.droppedSteps, 2);
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
  EXPECT_NEAR(in.tickAvgMs, 6.0, 1e-9);
  EXPECT_NEAR(in.tickP50Ms, 4.0, 1e-9);
  EXPECT_NEAR(in.tickP95Ms, 8.0, 1e-9);
  EXPECT_NEAR(in.tickP99Ms, 8.0, 1e-9);
  EXPECT_NEAR(in.tickMaxMs, 8.0, 1e-9);
  EXPECT_NEAR(in.scriptsMs, 2.0, 1e-9);
  EXPECT_NEAR(in.physicsMs, 2.0, 1e-9);
  EXPECT_NEAR(in.collisionsMs, 0.5, 1e-9);
  EXPECT_NEAR(in.replicationMs, 0.5, 1e-9);
}

TEST(StatsSummary, GatherCoversOnlyWhatFollowsThePreviousSummary)
{
  StatsRig rig;

  rig.lane.beginSample();
  rig.lane.addCounter(rig.counters.ticks, 3);
  {
    const profile::Scope tick(profile::internName("Server tick"));
    rig.advance(10ms);
  }
  rig.lane.endSample(true);
  const auto first = gatherStats(rig.lane, rig.counters, rig.cursor);

  rig.lane.beginSample();
  rig.lane.addCounter(rig.counters.ticks, 1);
  {
    const profile::Scope tick(profile::internName("Server tick"));
    rig.advance(2ms);
  }
  rig.lane.endSample(true);
  const auto second = gatherStats(rig.lane, rig.counters, rig.cursor);

  EXPECT_EQ(first.ticks, 3);
  EXPECT_NEAR(first.tickMaxMs, 10.0, 1e-9);
  EXPECT_EQ(second.ticks, 1);
  EXPECT_NEAR(second.tickMaxMs, 2.0, 1e-9);
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
  EXPECT_DOUBLE_EQ(in.tickMaxMs, 0.0);
}

TEST(StatsSummary, GatherWithNothingRecordedIsAllZero)
{
  StatsRig rig;

  const auto in = gatherStats(rig.lane, rig.counters, rig.cursor);

  EXPECT_EQ(in.ticks, 0);
  EXPECT_DOUBLE_EQ(in.coveredSeconds, 0.0);
  EXPECT_DOUBLE_EQ(in.tickAvgMs, 0.0);
}

TEST(StatsSummary, ANodeThatNeverAppearedPrintsZero)
{
  StatsRig rig;

  rig.lane.beginSample();
  rig.lane.addCounter(rig.counters.ticks, 1);
  {
    const profile::Scope tick(profile::internName("Server tick"));
    rig.timed("Physics", 4ms);
  }
  rig.lane.endSample(true);

  const auto in = gatherStats(rig.lane, rig.counters, rig.cursor);

  EXPECT_NEAR(in.physicsMs, 4.0, 1e-9);
  EXPECT_DOUBLE_EQ(in.scriptsMs, 0.0);
  EXPECT_DOUBLE_EQ(in.collisionsMs, 0.0);
  EXPECT_DOUBLE_EQ(in.replicationMs, 0.0);
  EXPECT_NE(formatStatsLine(in).find("scripts 0.00 physics 4.00 collisions 0.00 replication 0.00"), std::string::npos);
}

TEST(StatsSummary, GatherKeepsOnlyTheTickDurationsTheRingStillHolds)
{
  StatsRig rig(2);

  rig.lane.beginSample();
  rig.lane.addCounter(rig.counters.ticks, 3);
  for (const auto duration : { 30ms, 2ms, 4ms })
  {
    const profile::Scope tick(profile::internName("Server tick"));
    rig.advance(duration);
  }
  rig.lane.endSample(true);

  const auto in = gatherStats(rig.lane, rig.counters, rig.cursor);

  EXPECT_EQ(in.ticks, 3);
  EXPECT_NEAR(in.tickMaxMs, 4.0, 1e-9);
  EXPECT_NEAR(in.tickAvgMs, 3.0, 1e-9);
}
