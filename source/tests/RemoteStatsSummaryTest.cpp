#include <gtest/gtest.h>

#include <Protocol.h>
#include <RemoteStats.h>
#include <RemoteStatsSummary.h>
#include <ServerStats.h>
#include <SnapshotLog.h>

#include <chrono>
#include <cstdint>
#include <utility>
#include <vector>

namespace {
  using namespace std::chrono_literals;

  constexpr uint8_t typeA = 3;
  constexpr uint8_t typeB = 4;
  constexpr uint8_t typeC = 5;

  struct RemoteStatsSummaryTest : testing::Test
  {
    net::RemoteStats::Clock::time_point now{1000s};
    net::RemoteStats stats{[this] { return now; }};

    void SetUp() override
    {
      stats.subscribed();
    }

    // One batch covering `wallMicros` of server time.
    void deliver(const uint32_t wallMicros, std::vector<net::ServerStatsTraffic> out,
                 std::vector<net::ServerStatsTraffic> in = {})
    {
      net::ServerStats message;
      message.samples = {{wallMicros, 1, 0, {}}};
      message.out = std::move(out);
      message.in = std::move(in);
      stats.apply(message);
    }
  };
}

TEST_F(RemoteStatsSummaryTest, SumsBytesAndMessagesPerDirectionAndDividesByCoveredTime)
{
  deliver(500000, {{typeA, 2, 4000}, {typeB, 1, 1000}}, {{typeC, 5, 300}});
  now += 1s;
  deliver(500000, {{typeA, 3, 6000}}, {{typeC, 5, 300}});

  const auto summary = net::summarizeTraffic(stats, 10s);

  EXPECT_EQ(summary.sent.bytes, 11000u);
  EXPECT_EQ(summary.sent.messages, 6u);
  EXPECT_DOUBLE_EQ(summary.sent.coveredSeconds, 1.0);
  EXPECT_DOUBLE_EQ(summary.sent.bytesPerSecond, 11000.0);
  EXPECT_EQ(summary.received.bytes, 600u);
  EXPECT_EQ(summary.received.messages, 10u);
  EXPECT_DOUBLE_EQ(summary.received.bytesPerSecond, 600.0);
}

TEST_F(RemoteStatsSummaryTest, SortsTypesByBytesDescendingAndMergesRepeats)
{
  deliver(100000, {{typeB, 1, 100}, {typeA, 1, 100}, {typeC, 1, 900}});
  now += 1s;
  deliver(100000, {{typeB, 1, 500}});

  const auto summary = net::summarizeTraffic(stats, 10s);

  ASSERT_EQ(summary.sent.types.size(), 3u);
  EXPECT_EQ(summary.sent.types[0].type, typeC);
  EXPECT_EQ(summary.sent.types[1].type, typeB);
  EXPECT_EQ(summary.sent.types[1].bytes, 600u);
  EXPECT_EQ(summary.sent.types[1].messages, 2u);
  EXPECT_EQ(summary.sent.types[2].type, typeA);
}

TEST_F(RemoteStatsSummaryTest, BatchesOlderThanTheWindowBeforeTheNewestAreLeftOut)
{
  deliver(1000000, {{typeA, 1, 9000}});
  now += 6s;
  deliver(1000000, {{typeB, 1, 100}});
  now += 4s;
  deliver(1000000, {{typeB, 1, 200}});

  const auto summary = net::summarizeTraffic(stats, 5s);

  EXPECT_EQ(summary.sent.bytes, 300u);
  EXPECT_DOUBLE_EQ(summary.sent.coveredSeconds, 2.0);
  ASSERT_EQ(summary.sent.types.size(), 1u);
  EXPECT_EQ(summary.sent.types[0].type, typeB);

  EXPECT_EQ(net::summarizeTraffic(stats, 60s).sent.bytes, 9300u);
}

TEST_F(RemoteStatsSummaryTest, ABatchExactlyAtTheWindowEdgeIsKept)
{
  deliver(1000000, {{typeA, 1, 70}});
  now += 5s;
  deliver(1000000, {{typeA, 1, 30}});

  EXPECT_EQ(net::summarizeTraffic(stats, 5s).sent.bytes, 100u);
}

TEST_F(RemoteStatsSummaryTest, ZeroCoveredTimeGivesZeroRateButKeepsTheTotals)
{
  deliver(0, {{typeA, 1, 700}});

  const auto summary = net::summarizeTraffic(stats, 5s);

  EXPECT_EQ(summary.sent.bytes, 700u);
  EXPECT_DOUBLE_EQ(summary.sent.bytesPerSecond, 0.0);
}

TEST_F(RemoteStatsSummaryTest, NoBatchesGivesAnEmptySummary)
{
  const auto summary = net::summarizeTraffic(stats, 5s);

  EXPECT_EQ(summary.sent.bytes, 0u);
  EXPECT_TRUE(summary.sent.types.empty());
  EXPECT_TRUE(summary.received.types.empty());
}

TEST_F(RemoteStatsSummaryTest, SnapshotsWithinCountsOnlyTheRecentOnes)
{
  net::ServerStats message;
  message.snapshots = {{90000, 10, net::SnapshotReason::join, 5, 1},
                       {30000, 10, net::SnapshotReason::sceneEdit, 5, 1},
                       {1000, 10, net::SnapshotReason::sceneEdit, 5, 1}};
  stats.apply(message);

  EXPECT_EQ(net::snapshotsWithin(stats, now, 60s), 2u);
  EXPECT_EQ(net::snapshotsWithin(stats, now, 100s), 3u);
  EXPECT_EQ(net::snapshotsWithin(stats, now, 500ms), 0u);
}
