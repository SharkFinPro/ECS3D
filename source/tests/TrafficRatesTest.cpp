#include <gtest/gtest.h>

#include <Protocol.h>
#include <TrafficRates.h>

#include <chrono>
#include <cstddef>

namespace {
  using Clock = net::TrafficRateMeter::Clock;
  using std::chrono::milliseconds;

  constexpr auto delta = static_cast<std::size_t>(net::MessageType::stateDelta);
  constexpr auto snapshot = static_cast<std::size_t>(net::MessageType::snapshot);
  constexpr auto input = static_cast<std::size_t>(net::MessageType::inputState);
}

TEST(TrafficRateMeter, ReadsZeroUntilTheFirstIntervalPasses)
{
  net::TrafficRateMeter meter;
  net::TrafficTotals totals;
  const auto start = Clock::now();

  meter.update(totals, start);
  totals.bytes[delta] = 4000;
  meter.update(totals, start + milliseconds(100));

  EXPECT_EQ(meter.totalBytesPerSecond(), 0.0);
  EXPECT_TRUE(meter.rows().empty());

  meter.update(totals, start + milliseconds(500));

  EXPECT_NEAR(meter.totalBytesPerSecond(), 8000.0, 1e-6);
}

TEST(TrafficRateMeter, TypesAreSortedLargestFirstAndQuietOnesLeftOut)
{
  net::TrafficRateMeter meter;
  net::TrafficTotals totals;
  const auto start = Clock::now();

  meter.update(totals, start);
  totals.bytes[input] = 1000;
  totals.bytes[snapshot] = 9000;
  totals.bytes[delta] = 5000;
  meter.update(totals, start + milliseconds(1000));

  ASSERT_EQ(meter.rows().size(), 3u);
  EXPECT_EQ(meter.rows()[0].name, "snapshot");
  EXPECT_EQ(meter.rows()[1].name, "stateDelta");
  EXPECT_EQ(meter.rows()[2].name, "inputState");
  EXPECT_NEAR(meter.rows()[0].bytesPerSecond, 9000.0, 1e-6);
  EXPECT_NEAR(meter.totalBytesPerSecond(), 15000.0, 1e-6);
}

TEST(TrafficRateMeter, ARateIsTheDifferenceOverTheLastInterval)
{
  net::TrafficRateMeter meter;
  net::TrafficTotals totals;
  const auto start = Clock::now();

  meter.update(totals, start);
  totals.bytes[delta] = 1000;
  meter.update(totals, start + milliseconds(1000));
  totals.bytes[delta] = 1500;
  meter.update(totals, start + milliseconds(2000));

  EXPECT_NEAR(meter.totalBytesPerSecond(), 500.0, 1e-6);
  ASSERT_EQ(meter.rows().size(), 1u);
  EXPECT_NEAR(meter.rows()[0].bytesPerSecond, 500.0, 1e-6);
}

TEST(TrafficRateMeter, AnUpdateInsideTheMinimumIntervalChangesNothing)
{
  net::TrafficRateMeter meter;
  net::TrafficTotals totals;
  const auto start = Clock::now();

  meter.update(totals, start);
  totals.bytes[delta] = 1000;
  meter.update(totals, start + milliseconds(1000));
  ASSERT_NEAR(meter.totalBytesPerSecond(), 1000.0, 1e-6);

  totals.bytes[delta] = 5000;
  meter.update(totals, start + milliseconds(1499));

  EXPECT_NEAR(meter.totalBytesPerSecond(), 1000.0, 1e-6);

  meter.update(totals, start + milliseconds(1500));

  EXPECT_NEAR(meter.totalBytesPerSecond(), 8000.0, 1e-6);
}

TEST(TrafficRateMeter, AClockThatWentBackRestartsTheMeasurement)
{
  net::TrafficRateMeter meter;
  net::TrafficTotals totals;
  const auto start = Clock::now();

  meter.update(totals, start);
  totals.bytes[delta] = 1000;
  meter.update(totals, start + milliseconds(1000));
  ASSERT_GT(meter.totalBytesPerSecond(), 0.0);

  totals.bytes[delta] = 2000;
  meter.update(totals, start + milliseconds(500));

  EXPECT_EQ(meter.totalBytesPerSecond(), 0.0);
  EXPECT_TRUE(meter.rows().empty());

  totals.bytes[delta] = 2600;
  meter.update(totals, start + milliseconds(1000));

  EXPECT_NEAR(meter.totalBytesPerSecond(), 1200.0, 1e-6);
}

TEST(TrafficRateMeter, ALongGapIsAveragedOverItselfAndTheNextIntervalIsPerInterval)
{
  net::TrafficRateMeter meter;
  net::TrafficTotals totals;
  const auto start = Clock::now();
  const auto gap = std::chrono::minutes(10);

  meter.update(totals, start);
  totals.bytes[delta] = 600000;
  meter.update(totals, start + gap);

  EXPECT_NEAR(meter.totalBytesPerSecond(), 1000.0, 1e-6);

  totals.bytes[delta] = 600500;
  meter.update(totals, start + gap + milliseconds(500));

  EXPECT_NEAR(meter.totalBytesPerSecond(), 1000.0, 1e-6);

  totals.bytes[delta] = 602500;
  meter.update(totals, start + gap + milliseconds(1000));

  EXPECT_NEAR(meter.totalBytesPerSecond(), 4000.0, 1e-6);
}

TEST(TrafficRateMeter, ACounterThatWentBackRestartsTheMeasurement)
{
  net::TrafficRateMeter meter;
  net::TrafficTotals totals;
  const auto start = Clock::now();

  meter.update(totals, start);
  totals.bytes[delta] = 1000;
  meter.update(totals, start + milliseconds(1000));
  ASSERT_GT(meter.totalBytesPerSecond(), 0.0);

  totals.bytes[delta] = 10;
  meter.update(totals, start + milliseconds(2000));

  EXPECT_EQ(meter.totalBytesPerSecond(), 0.0);
  EXPECT_TRUE(meter.rows().empty());

  totals.bytes[delta] = 210;
  meter.update(totals, start + milliseconds(3000));

  EXPECT_NEAR(meter.totalBytesPerSecond(), 200.0, 1e-6);
}
