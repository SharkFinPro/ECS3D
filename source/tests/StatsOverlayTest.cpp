#include <gtest/gtest.h>

#include <History.h>
#include <StatsOverlay.h>

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace {
  using Clock = statsOverlay::StateUpdateGap::Clock;
  using std::chrono::milliseconds;

  constexpr std::size_t counter = 2;

  profile::History historyOf(const std::initializer_list<int64_t> values)
  {
    profile::History history(32);
    uint64_t sequence = 1;

    for (const int64_t value : values)
    {
      profile::Sample sample;
      sample.sequence = sequence++;
      sample.counters[counter] = value;
      history.push(sample);
    }

    return history;
  }
}

TEST(StatsOverlayMode, CyclesOffCompactDetailedOff)
{
  EXPECT_EQ(nextStatsOverlayMode(StatsOverlayMode::off), StatsOverlayMode::compact);
  EXPECT_EQ(nextStatsOverlayMode(StatsOverlayMode::compact), StatsOverlayMode::detailed);
  EXPECT_EQ(nextStatsOverlayMode(StatsOverlayMode::detailed), StatsOverlayMode::off);
}

TEST(StatsOverlayMode, TheStatsFlagStartsCompact)
{
  EXPECT_EQ(initialStatsOverlayMode(true), StatsOverlayMode::compact);
  EXPECT_EQ(initialStatsOverlayMode(false), StatsOverlayMode::off);
}

TEST(StateUpdateGap, TheFirstDeltaRecordsNothing)
{
  statsOverlay::StateUpdateGap gap;
  const auto start = Clock::now();

  EXPECT_EQ(gap.onStateDelta(start), 0u);
  EXPECT_EQ(gap.takeFrameMax(), 0u);

  EXPECT_EQ(gap.onStateDelta(start + milliseconds(16)), 16000u);
  EXPECT_EQ(gap.takeFrameMax(), 16000u);
}

TEST(StateUpdateGap, TheFrameKeepsItsLargestGapAndTakingClearsIt)
{
  statsOverlay::StateUpdateGap gap;
  const auto start = Clock::now();

  gap.onStateDelta(start);
  gap.onStateDelta(start + milliseconds(10));
  gap.onStateDelta(start + milliseconds(40));
  gap.onStateDelta(start + milliseconds(45));

  EXPECT_EQ(gap.takeFrameMax(), 30000u);
  EXPECT_EQ(gap.takeFrameMax(), 0u);

  EXPECT_EQ(gap.onStateDelta(start + milliseconds(50)), 5000u);
  EXPECT_EQ(gap.takeFrameMax(), 5000u);
}

TEST(StateUpdateGap, AGapThatRoundsToZeroStillCounts)
{
  statsOverlay::StateUpdateGap gap;
  const auto start = Clock::now();

  gap.onStateDelta(start);

  EXPECT_EQ(gap.onStateDelta(start), 1u);
}

TEST(StateIntervalStats, FramesWithoutAStateUpdateAreSkipped)
{
  const auto history = historyOf({10000, 0, 20000, 0, 0, 30000});
  const auto stats = statsOverlay::summarizeIntervals(history, 10, counter);

  EXPECT_EQ(stats.count, 3u);
  EXPECT_NEAR(stats.averageMillis, 20.0, 1e-9);
  EXPECT_NEAR(stats.maxMillis, 30.0, 1e-9);
  EXPECT_NEAR(stats.p95Millis, 30.0, 1e-9);
}

TEST(StateIntervalStats, OnlyTheNewestWindowCounts)
{
  const auto history = historyOf({90000, 10000, 20000});
  const auto stats = statsOverlay::summarizeIntervals(history, 2, counter);

  EXPECT_EQ(stats.count, 2u);
  EXPECT_NEAR(stats.maxMillis, 20.0, 1e-9);
}

TEST(StateIntervalStats, NothingToAverageGivesAZeroCount)
{
  const auto empty = historyOf({});
  const auto quiet = historyOf({0, 0, 0});
  const auto other = historyOf({5000});

  EXPECT_EQ(statsOverlay::summarizeIntervals(empty, 10, counter).count, 0u);
  EXPECT_EQ(statsOverlay::summarizeIntervals(quiet, 10, counter).count, 0u);
  EXPECT_EQ(statsOverlay::summarizeIntervals(other, 10, counter + 1).count, 0u);
  EXPECT_EQ(statsOverlay::summarizeIntervals(other, 10, profile::maxCounters).count, 0u);
  EXPECT_EQ(statsOverlay::summarizeIntervals(other, 10, counter).count, 1u);
}

TEST(StatsOverlayScale, TheP99WallComesFromTheNewestWindow)
{
  profile::History history(8);
  for (const uint32_t wall : {90000u, 4000u, 6000u})
  {
    profile::Sample sample;
    sample.wallMicros = wall;
    history.push(sample);
  }

  EXPECT_EQ(statsOverlay::p99WallMicros(history, 3), 90000u);
  EXPECT_EQ(statsOverlay::p99WallMicros(history, 2), 6000u);
  EXPECT_EQ(statsOverlay::p99WallMicros(profile::History(4), 3), 0u);
}
