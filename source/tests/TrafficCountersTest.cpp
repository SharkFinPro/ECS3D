#include <gtest/gtest.h>

#include <Protocol.h>
#include <TrafficCounters.h>

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>

namespace {
  constexpr auto delta = static_cast<std::size_t>(net::MessageType::stateDelta);
  constexpr auto serverLogIndex = static_cast<std::size_t>(net::MessageType::serverLog);
}

TEST(MessageTypeName, EveryTypeBelowTheCountHasADistinctRealName)
{
  std::set<std::string> names;
  for (std::size_t i = 0; i < net::messageTypeCount; ++i)
  {
    const auto name = net::messageTypeName(static_cast<net::MessageType>(i));
    EXPECT_NE(name, "unknown") << i;
    names.insert(std::string(name));
  }

  EXPECT_EQ(names.size(), net::messageTypeCount);
  EXPECT_EQ(net::messageTypeName(net::MessageType::stateDelta), "stateDelta");
  EXPECT_EQ(net::messageTypeName(net::MessageType::possessSlot), "possessSlot");
}

TEST(MessageTypeName, ATypeBeyondTheCountIsUnknown)
{
  EXPECT_EQ(net::messageTypeName(static_cast<net::MessageType>(net::messageTypeCount)), "unknown");
  EXPECT_EQ(net::messageTypeName(static_cast<net::MessageType>(255)), "unknown");
}

TEST(TrafficCounters, OutboundMultipliesByRecipientsAndCountsTheTypeByte)
{
  net::TrafficCounters counters;

  counters.countOut(net::MessageType::stateDelta, 99, 3);

  const auto out = counters.totalOut();
  EXPECT_EQ(out.messages[delta], 3u);
  EXPECT_EQ(out.bytes[delta], 300u);
  EXPECT_EQ(out.bytes[serverLogIndex], 0u);
  EXPECT_EQ(counters.totalIn().bytes[delta], 0u);
}

TEST(TrafficCounters, InboundCountsOneMessageWithItsTypeByte)
{
  net::TrafficCounters counters;

  counters.countIn(net::MessageType::serverLog, 9);
  counters.countIn(net::MessageType::serverLog, 0);

  const auto in = counters.totalIn();
  EXPECT_EQ(in.messages[serverLogIndex], 2u);
  EXPECT_EQ(in.bytes[serverLogIndex], 11u);
  EXPECT_EQ(counters.totalOut().messages[serverLogIndex], 0u);
}

TEST(TrafficCounters, ATypeBeyondTheTableLandsInTheUndefinedBucket)
{
  net::TrafficCounters counters;

  counters.countIn(static_cast<net::MessageType>(200), 4);
  counters.countOut(static_cast<net::MessageType>(net::messageTypeCount), 4, 2);

  EXPECT_EQ(counters.totalIn().bytes[0], 5u);
  EXPECT_EQ(counters.totalOut().bytes[0], 10u);
  EXPECT_EQ(counters.totalOut().messages[0], 2u);
}

TEST(TrafficCounters, RefusalsAreCountedApartFromBytes)
{
  net::TrafficCounters counters;

  counters.countRefused();
  counters.countRefused();

  const auto out = counters.totalOut();
  EXPECT_EQ(out.refused, 2u);
  EXPECT_EQ(out.bytes[0], 0u);
  EXPECT_EQ(counters.totalIn().refused, 0u);
}

TEST(TrafficCounters, TotalsOnlyGrowAcrossCalls)
{
  net::TrafficCounters counters;

  counters.countOut(net::MessageType::stateDelta, 10, 1);
  const auto first = counters.totalOut();
  counters.countOut(net::MessageType::stateDelta, 10, 0);
  const auto unchanged = counters.totalOut();
  counters.countOut(net::MessageType::stateDelta, 10, 2);
  const auto last = counters.totalOut();

  EXPECT_EQ(first.bytes[delta], 11u);
  EXPECT_EQ(unchanged.bytes[delta], 11u);
  EXPECT_EQ(last.bytes[delta], 33u);
}
