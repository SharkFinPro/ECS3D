#include <gtest/gtest.h>

#include "History.h"
#include "ProfileStats.h"

#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>

namespace {
  std::vector<uint32_t> oneToTwenty()
  {
    std::vector<uint32_t> values(20);
    std::iota(values.begin(), values.end(), 1u);
    return values;
  }

  profile::History historyOfWalls(const std::vector<uint32_t>& walls, const std::size_t capacity = 64)
  {
    profile::History history(capacity);
    uint64_t sequence = 1;

    for (const auto wall : walls)
    {
      profile::Sample sample;
      sample.sequence = sequence++;
      sample.wallMicros = wall;
      sample.nodeMicros[1] = wall / 2;
      history.push(sample);
    }

    return history;
  }
}

TEST(ProfileStats, PercentileOfNothingIsZero)
{
  EXPECT_EQ(profile::percentile({}, 95.0), 0u);
}

TEST(ProfileStats, PercentileOfOneValueIsThatValue)
{
  EXPECT_EQ(profile::percentile({42}, 0.0), 42u);
  EXPECT_EQ(profile::percentile({42}, 50.0), 42u);
  EXPECT_EQ(profile::percentile({42}, 100.0), 42u);
}

TEST(ProfileStats, PercentileUsesNearestRank)
{
  const auto values = oneToTwenty();

  EXPECT_EQ(profile::percentile(values, 95.0), 19u);
  EXPECT_EQ(profile::percentile(values, 50.0), 10u);
  EXPECT_EQ(profile::percentile(values, 0.0), 1u);
  EXPECT_EQ(profile::percentile(values, 100.0), 20u);
  EXPECT_EQ(profile::percentile(values, 96.0), 20u);
}

TEST(ProfileStats, PercentileNearestRankIsExactAtBoundaries)
{
  std::vector<uint32_t> values(100);
  std::iota(values.begin(), values.end(), 1u);

  EXPECT_EQ(profile::percentile(values, 95.0), 95u);
  EXPECT_EQ(profile::percentile(values, 96.0), 96u);
  EXPECT_EQ(profile::percentile(values, 1.0), 1u);
}

TEST(ProfileStats, PercentileOfNaNIsTheSmallestValue)
{
  EXPECT_EQ(profile::percentile({3, 1, 2}, std::numeric_limits<double>::quiet_NaN()), 1u);
  EXPECT_EQ(profile::percentile({3, 1, 2}, 100.0), 3u);
}

TEST(ProfileStats, PercentileDoesNotNeedSortedInput)
{
  EXPECT_EQ(profile::percentile({9, 1, 7, 3, 5}, 50.0), 5u);
  EXPECT_EQ(profile::percentile({9, 1, 7, 3, 5}, 100.0), 9u);
}

TEST(ProfileStats, ExactlyTwiceTheMedianIsNotAHitch)
{
  EXPECT_FALSE(profile::isHitch(200, 100));
  EXPECT_TRUE(profile::isHitch(201, 100));
  EXPECT_FALSE(profile::isHitch(50, 100));
}

TEST(ProfileStats, AZeroMedianIsNeverAHitch)
{
  EXPECT_FALSE(profile::isHitch(1000, 0));
  EXPECT_TRUE(profile::isHitch(1000, 1));
}

TEST(ProfileStats, SummaryOfAnEmptyHistoryIsZero)
{
  const profile::History history(4);
  const auto summary = profile::summarizeWall(history, 10);

  EXPECT_EQ(summary.samples, 0u);
  EXPECT_EQ(summary.last, 0u);
  EXPECT_EQ(summary.max, 0u);
  EXPECT_EQ(summary.p95, 0u);
  EXPECT_DOUBLE_EQ(summary.average, 0.0);
  EXPECT_EQ(profile::countHitches(history, 10), 0u);
}

TEST(ProfileStats, SummaryCoversTheWholeHistoryWhenTheWindowIsLarger)
{
  const auto history = historyOfWalls(oneToTwenty());
  const auto summary = profile::summarizeWall(history, 100);

  EXPECT_EQ(summary.samples, 20u);
  EXPECT_EQ(summary.last, 20u);
  EXPECT_EQ(summary.max, 20u);
  EXPECT_EQ(summary.p95, 19u);
  EXPECT_DOUBLE_EQ(summary.average, 10.5);
}

TEST(ProfileStats, SummaryCoversOnlyTheNewestSamplesWhenTheWindowIsSmaller)
{
  const auto history = historyOfWalls({1000, 10, 20, 30});
  const auto summary = profile::summarizeWall(history, 3);

  EXPECT_EQ(summary.samples, 3u);
  EXPECT_EQ(summary.last, 30u);
  EXPECT_EQ(summary.max, 30u);
  EXPECT_DOUBLE_EQ(summary.average, 20.0);
}

TEST(ProfileStats, NodeSummaryReadsThatNodesMicros)
{
  const auto history = historyOfWalls({100, 200, 400});
  const auto summary = profile::summarizeNode(history, 1, 2);

  EXPECT_EQ(summary.samples, 2u);
  EXPECT_EQ(summary.last, 200u);
  EXPECT_EQ(summary.max, 200u);
  EXPECT_DOUBLE_EQ(summary.average, 150.0);
  EXPECT_EQ(profile::summarizeNode(history, 2, 2).max, 0u);
  EXPECT_EQ(profile::summarizeNode(history, 2, 2).samples, 2u);
  EXPECT_EQ(profile::summarizeNode(history, profile::maxNodes, 2).samples, 0u);
  EXPECT_EQ(profile::summarizeNode(history, profile::maxNodes, 2).max, 0u);
}

TEST(ProfileStats, ASpikeIsCountedAsAHitch)
{
  std::vector<uint32_t> walls(10, 1000);
  walls.push_back(5000);

  const auto history = historyOfWalls(walls);

  EXPECT_EQ(profile::countHitches(history, 11), 1u);
}

TEST(ProfileStats, SteadyTimesHaveNoHitches)
{
  auto walls = std::vector<uint32_t>(11, 1000);
  EXPECT_EQ(profile::countHitches(historyOfWalls(walls), 11), 0u);

  walls.back() = 2001;
  EXPECT_EQ(profile::countHitches(historyOfWalls(walls), 11), 1u);
}

TEST(ProfileStats, TheHitchWindowExcludesOlderSpikes)
{
  std::vector<uint32_t> walls{5000};
  walls.insert(walls.end(), 10, 1000);

  const auto history = historyOfWalls(walls);

  EXPECT_EQ(profile::countHitches(history, 11), 1u);
  EXPECT_EQ(profile::countHitches(history, 10), 0u);
}
