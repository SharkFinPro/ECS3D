#include <gtest/gtest.h>

#include <Protocol.h>
#include <ServerStats.h>
#include <SnapshotLog.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
  net::ServerStats sampleStats()
  {
    net::ServerStats stats;
    stats.flags = net::serverStatsFlags::debugBuild | net::serverStatsFlags::sceneRunning |
                  net::serverStatsFlags::nodeTable;
    stats.nodeTableEpoch = 4;
    stats.hasNodeTable = true;
    stats.nodes = {{0, "Server tick"}, {1, "Physics"}, {0, "Replication"}};
    stats.samples = {
      {16000, 1, 0, {{1, 9000, 1}, {2, 4000, 3}}},
      {17000, 2, 1, {{3, 500, 1}}}
    };
    stats.tickMicros = {8000, 9500, 70000};
    stats.out = {{static_cast<uint8_t>(net::MessageType::stateDelta), 50, 123456789012ull},
                 {static_cast<uint8_t>(net::MessageType::snapshot), 1, 900}};
    stats.in = {{static_cast<uint8_t>(net::MessageType::inputState), 12, 480}};
    stats.snapshots = {{1500, 4096, net::SnapshotReason::join, 800, 1},
                       {20, 8192, net::SnapshotReason::scriptReload, 1200, 3}};
    stats.connections = 3;
    stats.editors = 2;
    stats.objects = 85;
    return stats;
  }

  void expectSame(const net::ServerStats& a, const net::ServerStats& b)
  {
    EXPECT_EQ(a.flags, b.flags);
    EXPECT_EQ(a.nodeTableEpoch, b.nodeTableEpoch);
    EXPECT_EQ(a.hasNodeTable, b.hasNodeTable);

    ASSERT_EQ(a.nodes.size(), b.nodes.size());
    for (std::size_t i = 0; i < a.nodes.size(); ++i)
    {
      EXPECT_EQ(a.nodes[i].parent, b.nodes[i].parent) << i;
      EXPECT_EQ(a.nodes[i].name, b.nodes[i].name) << i;
    }

    ASSERT_EQ(a.samples.size(), b.samples.size());
    for (std::size_t i = 0; i < a.samples.size(); ++i)
    {
      EXPECT_EQ(a.samples[i].wallMicros, b.samples[i].wallMicros) << i;
      EXPECT_EQ(a.samples[i].ticks, b.samples[i].ticks) << i;
      EXPECT_EQ(a.samples[i].droppedSteps, b.samples[i].droppedSteps) << i;
      ASSERT_EQ(a.samples[i].nodes.size(), b.samples[i].nodes.size()) << i;
      for (std::size_t j = 0; j < a.samples[i].nodes.size(); ++j)
      {
        EXPECT_EQ(a.samples[i].nodes[j].node, b.samples[i].nodes[j].node);
        EXPECT_EQ(a.samples[i].nodes[j].micros, b.samples[i].nodes[j].micros);
        EXPECT_EQ(a.samples[i].nodes[j].calls, b.samples[i].nodes[j].calls);
      }
    }

    EXPECT_EQ(a.tickMicros, b.tickMicros);

    for (const auto& [left, right] : {std::pair{&a.out, &b.out}, std::pair{&a.in, &b.in}})
    {
      ASSERT_EQ(left->size(), right->size());
      for (std::size_t i = 0; i < left->size(); ++i)
      {
        EXPECT_EQ((*left)[i].type, (*right)[i].type);
        EXPECT_EQ((*left)[i].messages, (*right)[i].messages);
        EXPECT_EQ((*left)[i].bytes, (*right)[i].bytes);
      }
    }

    ASSERT_EQ(a.snapshots.size(), b.snapshots.size());
    for (std::size_t i = 0; i < a.snapshots.size(); ++i)
    {
      EXPECT_EQ(a.snapshots[i].ageMillis, b.snapshots[i].ageMillis);
      EXPECT_EQ(a.snapshots[i].bytes, b.snapshots[i].bytes);
      EXPECT_EQ(a.snapshots[i].reason, b.snapshots[i].reason);
      EXPECT_EQ(a.snapshots[i].buildMicros, b.snapshots[i].buildMicros);
      EXPECT_EQ(a.snapshots[i].recipients, b.snapshots[i].recipients);
    }

    EXPECT_EQ(a.connections, b.connections);
    EXPECT_EQ(a.editors, b.editors);
    EXPECT_EQ(a.objects, b.objects);
  }

  // Bytes of a message with a valid layout, to build hostile variants by hand.
  struct Builder {
    net::Message message{net::MessageType::serverStats};

    Builder& header(const uint16_t version = net::serverStatsVersion, const uint8_t flags = 0)
    {
      message.write(version);
      message.write(flags);
      message.write(static_cast<uint32_t>(1));
      return *this;
    }

    // The sections after the node table, all empty.
    Builder& emptyRest()
    {
      message.write(static_cast<uint16_t>(0));
      message.write(static_cast<uint16_t>(0));
      message.write(static_cast<uint8_t>(0));
      message.write(static_cast<uint8_t>(0));
      message.write(static_cast<uint8_t>(0));
      message.write(static_cast<uint16_t>(0));
      message.write(static_cast<uint16_t>(0));
      message.write(static_cast<uint32_t>(0));
      return *this;
    }
  };

  net::Message tableWith(const std::size_t nodeCount, const uint16_t parentOfLast = 0)
  {
    Builder b;
    b.header(net::serverStatsVersion, net::serverStatsFlags::nodeTable);
    b.message.write(static_cast<uint16_t>(nodeCount));
    for (std::size_t i = 0; i < nodeCount; ++i)
    {
      b.message.write(i + 1 == nodeCount ? parentOfLast : static_cast<uint16_t>(0));
      b.message.writeString("n");
    }

    b.emptyRest();
    return b.message;
  }

  // One sample whose single entry names `node`.
  net::Message sampleNaming(const uint16_t node)
  {
    Builder b;
    b.header();
    b.message.write(static_cast<uint16_t>(1));
    b.message.write(static_cast<uint32_t>(1));
    b.message.write(static_cast<uint8_t>(1));
    b.message.write(static_cast<uint8_t>(0));
    b.message.write(static_cast<uint16_t>(1));
    b.message.write(node);
    b.message.write(static_cast<uint32_t>(5));
    b.message.write(static_cast<uint16_t>(1));
    b.message.write(static_cast<uint16_t>(0));
    b.message.write(static_cast<uint8_t>(0));
    b.message.write(static_cast<uint8_t>(0));
    b.message.write(static_cast<uint8_t>(0));
    b.message.write(static_cast<uint16_t>(0));
    b.message.write(static_cast<uint16_t>(0));
    b.message.write(static_cast<uint32_t>(0));
    return b.message;
  }

  net::Message samplesCount(const uint16_t count)
  {
    Builder b;
    b.header();
    b.message.write(count);
    for (uint16_t i = 0; i < count; ++i)
    {
      b.message.write(static_cast<uint32_t>(0));
      b.message.write(static_cast<uint8_t>(0));
      b.message.write(static_cast<uint8_t>(0));
      b.message.write(static_cast<uint16_t>(0));
    }

    b.message.write(static_cast<uint16_t>(0));
    b.message.write(static_cast<uint8_t>(0));
    b.message.write(static_cast<uint8_t>(0));
    b.message.write(static_cast<uint8_t>(0));
    b.message.write(static_cast<uint16_t>(0));
    b.message.write(static_cast<uint16_t>(0));
    b.message.write(static_cast<uint32_t>(0));
    return b.message;
  }

  net::Message ticksCount(const uint16_t count)
  {
    Builder b;
    b.header();
    b.message.write(static_cast<uint16_t>(0));
    b.message.write(count);
    for (uint16_t i = 0; i < count; ++i)
    {
      b.message.write(static_cast<uint32_t>(i));
    }

    b.message.write(static_cast<uint8_t>(0));
    b.message.write(static_cast<uint8_t>(0));
    b.message.write(static_cast<uint8_t>(0));
    b.message.write(static_cast<uint16_t>(0));
    b.message.write(static_cast<uint16_t>(0));
    b.message.write(static_cast<uint32_t>(0));
    return b.message;
  }

  net::Message snapshotsWith(const uint8_t count, const uint8_t reason)
  {
    Builder b;
    b.header();
    b.message.write(static_cast<uint16_t>(0));
    b.message.write(static_cast<uint16_t>(0));
    b.message.write(static_cast<uint8_t>(0));
    b.message.write(static_cast<uint8_t>(0));
    b.message.write(count);
    for (uint8_t i = 0; i < count; ++i)
    {
      b.message.write(static_cast<uint32_t>(1));
      b.message.write(static_cast<uint32_t>(2));
      b.message.write(reason);
      b.message.write(static_cast<uint32_t>(3));
      b.message.write(static_cast<uint16_t>(4));
    }

    b.message.write(static_cast<uint16_t>(0));
    b.message.write(static_cast<uint16_t>(0));
    b.message.write(static_cast<uint32_t>(0));
    return b.message;
  }

  net::Message withVersion(const net::Message& source, const uint16_t version)
  {
    std::vector<uint8_t> bytes(source.bytes().begin(), source.bytes().end());
    std::memcpy(bytes.data(), &version, sizeof(version));
    return {net::MessageType::serverStats, bytes};
  }

  net::Message prefix(const net::Message& source, const std::size_t length)
  {
    return {net::MessageType::serverStats, source.bytes().first(length)};
  }
}

TEST(ServerStats, RoundTripsEveryFieldWithTheNodeTable)
{
  const auto stats = sampleStats();

  const auto packed = net::packServerStats(stats, true);
  const auto restored = net::unpackServerStats(packed);

  EXPECT_EQ(packed.getType(), net::MessageType::serverStats);
  expectSame(restored, stats);
  EXPECT_TRUE(restored.hasNodeTable);
  EXPECT_EQ(restored.nodes.size(), 3u);
}

TEST(ServerStats, WithoutTheNodeTableOnlyTheTableIsLeftOut)
{
  const auto stats = sampleStats();

  const auto withTable = net::packServerStats(stats, true);
  const auto without = net::packServerStats(stats, false);
  const auto restored = net::unpackServerStats(without);

  auto expected = stats;
  expected.nodes.clear();
  expected.hasNodeTable = false;
  expected.flags = static_cast<uint8_t>(stats.flags & ~net::serverStatsFlags::nodeTable);

  expectSame(restored, expected);
  EXPECT_EQ(restored.flags & net::serverStatsFlags::nodeTable, 0);
  EXPECT_LT(without.size(), withTable.size());
  EXPECT_TRUE(net::unpackServerStats(withTable).hasNodeTable);
}

TEST(ServerStats, PackSetsTheTableBitFromTheArgumentNotFromTheFlags)
{
  auto stats = sampleStats();
  stats.flags = 0;

  EXPECT_TRUE(net::unpackServerStats(net::packServerStats(stats, true)).hasNodeTable);

  stats.flags = net::serverStatsFlags::nodeTable;
  EXPECT_FALSE(net::unpackServerStats(net::packServerStats(stats, false)).hasNodeTable);
}

TEST(ServerStats, EmptySectionsRoundTrip)
{
  net::ServerStats stats;
  stats.nodeTableEpoch = 1;

  const auto restored = net::unpackServerStats(net::packServerStats(stats, false));

  expectSame(restored, stats);
  EXPECT_TRUE(restored.samples.empty());
  EXPECT_TRUE(restored.out.empty());
  EXPECT_TRUE(restored.snapshots.empty());

  const auto populated = net::unpackServerStats(net::packServerStats(sampleStats(), false));
  EXPECT_FALSE(populated.samples.empty());
}

TEST(ServerStats, EveryTruncatedPrefixIsRefused)
{
  const auto full = net::packServerStats(sampleStats(), true);

  EXPECT_NO_THROW(static_cast<void>(net::unpackServerStats(full)));
  for (std::size_t length = 0; length < full.size(); ++length)
  {
    EXPECT_THROW(static_cast<void>(net::unpackServerStats(prefix(full, length))), std::runtime_error) << length;
  }
}

TEST(ServerStats, OnlyVersionOneIsAccepted)
{
  const auto valid = net::packServerStats(sampleStats(), true);

  EXPECT_NO_THROW(static_cast<void>(net::unpackServerStats(withVersion(valid, 1))));
  EXPECT_THROW(static_cast<void>(net::unpackServerStats(withVersion(valid, 2))), std::runtime_error);
  EXPECT_THROW(static_cast<void>(net::unpackServerStats(withVersion(valid, 0))), std::runtime_error);
}

TEST(ServerStats, TheNodeTableIsCappedAtTheIdsBelowTheNodeCap)
{
  EXPECT_NO_THROW(static_cast<void>(net::unpackServerStats(tableWith(net::maxServerStatsNodes - 1))));
  EXPECT_THROW(static_cast<void>(net::unpackServerStats(tableWith(net::maxServerStatsNodes))), std::runtime_error);
  EXPECT_THROW(static_cast<void>(net::unpackServerStats(tableWith(net::maxServerStatsNodes + 1))),
               std::runtime_error);
}

TEST(ServerStats, ANodeParentMustBeAnEarlierNode)
{
  EXPECT_NO_THROW(static_cast<void>(net::unpackServerStats(tableWith(3, 2))));
  EXPECT_THROW(static_cast<void>(net::unpackServerStats(tableWith(3, 3))), std::runtime_error);
  EXPECT_THROW(static_cast<void>(net::unpackServerStats(tableWith(3, 40))), std::runtime_error);
}

TEST(ServerStats, ASampleEntryMustNameANodeBelowTheCap)
{
  const auto accepted = net::unpackServerStats(sampleNaming(net::maxServerStatsNodes - 1));
  ASSERT_EQ(accepted.samples.size(), 1u);
  ASSERT_EQ(accepted.samples[0].nodes.size(), 1u);
  EXPECT_EQ(accepted.samples[0].nodes[0].node, net::maxServerStatsNodes - 1);

  EXPECT_THROW(static_cast<void>(net::unpackServerStats(sampleNaming(net::maxServerStatsNodes))),
               std::runtime_error);
}

TEST(ServerStats, TheSampleCountIsCapped)
{
  EXPECT_EQ(net::unpackServerStats(samplesCount(net::maxServerStatsSamples)).samples.size(),
            net::maxServerStatsSamples);
  EXPECT_THROW(static_cast<void>(net::unpackServerStats(samplesCount(net::maxServerStatsSamples + 1))),
               std::runtime_error);
}

TEST(ServerStats, TheTickCountIsCapped)
{
  EXPECT_EQ(net::unpackServerStats(ticksCount(net::maxServerStatsTicks)).tickMicros.size(),
            net::maxServerStatsTicks);
  EXPECT_THROW(static_cast<void>(net::unpackServerStats(ticksCount(net::maxServerStatsTicks + 1))),
               std::runtime_error);
}

TEST(ServerStats, TheSnapshotCountIsCappedAndTheReasonMustBeKnown)
{
  const auto otherReason = static_cast<uint8_t>(net::SnapshotReason::other);

  EXPECT_EQ(net::unpackServerStats(snapshotsWith(net::maxServerStatsSnapshots, otherReason)).snapshots.size(),
            net::maxServerStatsSnapshots);
  EXPECT_THROW(static_cast<void>(net::unpackServerStats(snapshotsWith(net::maxServerStatsSnapshots + 1,
                                                                      otherReason))),
               std::runtime_error);

  const auto accepted = net::unpackServerStats(snapshotsWith(1, otherReason));
  EXPECT_EQ(accepted.snapshots[0].reason, net::SnapshotReason::other);
  EXPECT_THROW(static_cast<void>(net::unpackServerStats(snapshotsWith(1, otherReason + 1))), std::runtime_error);
}

TEST(ServerStats, ACountPastWhatTheRemainingBytesCouldHoldIsRefusedBeforeReserving)
{
  Builder b;
  b.header();
  b.message.write(static_cast<uint16_t>(net::maxServerStatsSamples));

  EXPECT_THROW(static_cast<void>(net::unpackServerStats(b.message)), std::runtime_error);
}

TEST(ServerStats, PackRefusesAVectorPastItsCap)
{
  auto stats = sampleStats();
  EXPECT_NO_THROW(static_cast<void>(net::packServerStats(stats, true)));

  auto tooManySamples = stats;
  tooManySamples.samples.resize(net::maxServerStatsSamples + 1);
  EXPECT_THROW(static_cast<void>(net::packServerStats(tooManySamples, true)), std::length_error);

  auto tooManyTicks = stats;
  tooManyTicks.tickMicros.resize(net::maxServerStatsTicks + 1);
  EXPECT_THROW(static_cast<void>(net::packServerStats(tooManyTicks, true)), std::length_error);

  auto tooManySnapshots = stats;
  tooManySnapshots.snapshots.resize(net::maxServerStatsSnapshots + 1);
  EXPECT_THROW(static_cast<void>(net::packServerStats(tooManySnapshots, true)), std::length_error);

  auto tooManyNodes = stats;
  tooManyNodes.nodes.resize(net::maxServerStatsNodes);
  EXPECT_THROW(static_cast<void>(net::packServerStats(tooManyNodes, true)), std::length_error);
  EXPECT_NO_THROW(static_cast<void>(net::packServerStats(tooManyNodes, false)));
}

TEST(ServerStats, SubscribeRoundTripsBothWays)
{
  const auto on = net::packStatsSubscribe(true);
  const auto off = net::packStatsSubscribe(false);

  EXPECT_EQ(on.getType(), net::MessageType::statsSubscribe);
  EXPECT_TRUE(net::unpackStatsSubscribe(on));
  EXPECT_FALSE(net::unpackStatsSubscribe(off));
  EXPECT_THROW(static_cast<void>(net::unpackStatsSubscribe(net::Message(net::MessageType::statsSubscribe))),
               std::runtime_error);
}
