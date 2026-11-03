#include <gtest/gtest.h>

#include <Protocol.h>
#include <RemoteStats.h>
#include <ServerStats.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
  using namespace std::chrono_literals;
  using State = net::RemoteStats::State;

  struct RemoteStatsTest : testing::Test
  {
    net::RemoteStats::Clock::time_point now{1000s};
    net::RemoteStats stats{[this] { return now; }};

    static net::ServerStats table()
    {
      net::ServerStats message;
      message.flags = net::serverStatsFlags::nodeTable;
      message.nodeTableEpoch = 3;
      message.hasNodeTable = true;
      message.nodes = {{0, "Server tick"}, {1, "Physics"}};
      return message;
    }

    static net::ServerStats withSample(const uint32_t wallMicros, const uint16_t node = 1)
    {
      net::ServerStats message;
      message.samples = {{wallMicros, 2, 1, {{node, 700, 4}}}};
      return message;
    }
  };

  net::Message withVersion(const net::Message& source, const uint16_t version)
  {
    std::vector<uint8_t> bytes(source.bytes().begin(), source.bytes().end());
    bytes[0] = static_cast<uint8_t>(version & 0xFF);
    bytes[1] = static_cast<uint8_t>(version >> 8);
    return {net::MessageType::serverStats, bytes};
  }
}

TEST_F(RemoteStatsTest, IdleAtConstruction)
{
  EXPECT_EQ(stats.state(), State::idle);
  EXPECT_EQ(stats.nodeCount(), 1u);
}

TEST_F(RemoteStatsTest, WaitingBecomesUnsupportedAfterTwoSeconds)
{
  stats.subscribed();
  EXPECT_EQ(stats.state(), State::waiting);

  now += 1999ms;
  EXPECT_EQ(stats.state(), State::waiting);

  now += 1ms;
  EXPECT_EQ(stats.state(), State::unsupported);
}

TEST_F(RemoteStatsTest, AMessageAfterUnsupportedTurnsLive)
{
  stats.subscribed();
  now += 3s;
  ASSERT_EQ(stats.state(), State::unsupported);

  stats.apply(withSample(100));
  EXPECT_EQ(stats.state(), State::live);

  now += 10s;
  EXPECT_EQ(stats.state(), State::live);
}

TEST_F(RemoteStatsTest, UnsubscribedAndDisconnectedStates)
{
  stats.subscribed();
  stats.apply(withSample(100, 0));
  ASSERT_EQ(stats.history().size(), 1u);

  stats.disconnected();
  EXPECT_EQ(stats.state(), State::disconnected);
  EXPECT_EQ(stats.history().size(), 1u);

  stats.subscribed();
  stats.unsubscribed();
  EXPECT_EQ(stats.state(), State::idle);
}

TEST_F(RemoteStatsTest, MessagesWhileIdleOrDisconnectedChangeNothing)
{
  stats.apply(withSample(100, 0));
  EXPECT_EQ(stats.history().size(), 0u);
  EXPECT_EQ(stats.state(), State::idle);

  stats.subscribed();
  stats.disconnected();
  stats.apply(withSample(100, 0));
  EXPECT_EQ(stats.history().size(), 0u);
  EXPECT_EQ(stats.state(), State::disconnected);

  stats.subscribed();
  stats.apply(withSample(100, 0));
  EXPECT_EQ(stats.history().size(), 1u);
}

TEST_F(RemoteStatsTest, SubscribingClearsEverything)
{
  stats.subscribed();
  auto message = table();
  message.samples = {{100, 1, 0, {{1, 5, 1}}}};
  message.tickMicros = {8000};
  message.snapshots = {{10, 100, net::SnapshotReason::join, 5, 1}};
  message.connections = 2;
  stats.apply(message);
  ASSERT_EQ(stats.history().size(), 1u);
  ASSERT_EQ(stats.tickDurations().size(), 1u);
  ASSERT_EQ(stats.nodeCount(), 3u);
  ASSERT_EQ(stats.trafficBatches().size(), 1u);
  ASSERT_EQ(stats.snapshots().size(), 1u);

  stats.subscribed();

  EXPECT_EQ(stats.history().size(), 0u);
  EXPECT_EQ(stats.tickDurations().size(), 0u);
  EXPECT_EQ(stats.nodeCount(), 1u);
  EXPECT_EQ(stats.nodeTableEpoch(), 0u);
  EXPECT_TRUE(stats.trafficBatches().empty());
  EXPECT_TRUE(stats.snapshots().empty());
  EXPECT_EQ(stats.connections(), 0u);
  EXPECT_EQ(stats.state(), State::waiting);
}

TEST_F(RemoteStatsTest, TheTableIsKeptByLaterMessagesWithoutOne)
{
  stats.subscribed();
  stats.apply(table());
  stats.apply(withSample(100));

  ASSERT_EQ(stats.nodeCount(), 3u);
  EXPECT_EQ(stats.nodeTableEpoch(), 3u);
  EXPECT_EQ(stats.nodeName(0), "");
  EXPECT_EQ(stats.nodeName(1), "Server tick");
  EXPECT_EQ(stats.nodeName(2), "Physics");
  EXPECT_EQ(stats.nodeParent(2), 1u);
  ASSERT_EQ(stats.history().size(), 1u);
  EXPECT_EQ(stats.history().newest().nodeMicros[1], 700u);
}

TEST_F(RemoteStatsTest, ANewTableReplacesTheHeldOne)
{
  stats.subscribed();
  stats.apply(table());

  auto replacement = table();
  replacement.nodeTableEpoch = 4;
  replacement.nodes = {{0, "Only"}};
  stats.apply(replacement);

  EXPECT_EQ(stats.nodeCount(), 2u);
  EXPECT_EQ(stats.nodeName(1), "Only");
  EXPECT_EQ(stats.nodeTableEpoch(), 4u);
}

TEST_F(RemoteStatsTest, SamplesBecomeHistoryEntries)
{
  stats.subscribed();
  auto message = table();
  message.samples = {
    {16000, 1, 0, {{1, 9000, 1}, {2, 4000, 3}}},
    {17000, 2, 1, {{2, 500, 1}}}
  };
  stats.apply(message);
  stats.apply(withSample(1000));

  ASSERT_EQ(stats.history().size(), 3u);
  const auto& first = stats.history().at(0);
  const auto& second = stats.history().at(1);
  const auto& third = stats.history().at(2);

  EXPECT_EQ(first.sequence, 1u);
  EXPECT_EQ(second.sequence, 2u);
  EXPECT_EQ(third.sequence, 3u);
  EXPECT_EQ(first.startNanos, 0);
  EXPECT_EQ(second.startNanos, 16000 * 1000);
  EXPECT_EQ(third.startNanos, (16000 + 17000) * 1000);
  EXPECT_EQ(first.wallMicros, 16000u);
  EXPECT_EQ(first.nodeMicros[1], 9000u);
  EXPECT_EQ(first.nodeCalls[1], 1u);
  EXPECT_EQ(first.nodeMicros[2], 4000u);
  EXPECT_EQ(first.nodeCalls[2], 3u);
  EXPECT_EQ(second.nodeMicros[1], 0u);
  EXPECT_EQ(second.nodeMicros[2], 500u);
  EXPECT_EQ(first.counters[net::RemoteStats::ticksCounter], 1);
  EXPECT_EQ(first.counters[net::RemoteStats::droppedStepsCounter], 0);
  EXPECT_EQ(second.counters[net::RemoteStats::ticksCounter], 2);
  EXPECT_EQ(second.counters[net::RemoteStats::droppedStepsCounter], 1);
}

TEST_F(RemoteStatsTest, EntriesPastTheTableAreSkippedAndCounted)
{
  stats.subscribed();
  stats.apply(withSample(100, 0));
  EXPECT_EQ(stats.unknownNodeEntries(), 0u);
  EXPECT_EQ(stats.history().newest().nodeMicros[0], 700u);

  stats.apply(withSample(100, 1));
  EXPECT_EQ(stats.unknownNodeEntries(), 1u);
  EXPECT_EQ(stats.history().newest().nodeMicros[1], 0u);

  stats.apply(table());
  stats.apply(withSample(100, 2));
  EXPECT_EQ(stats.unknownNodeEntries(), 1u);
  EXPECT_EQ(stats.history().newest().nodeMicros[2], 700u);

  stats.apply(withSample(100, 3));
  EXPECT_EQ(stats.unknownNodeEntries(), 2u);
  EXPECT_EQ(stats.history().newest().nodeMicros[3], 0u);
}

TEST_F(RemoteStatsTest, TicksKeepOrderAndTheRingCapacity)
{
  stats.subscribed();
  net::ServerStats message;
  message.tickMicros = {5, 6, 7};
  stats.apply(message);

  EXPECT_EQ(stats.tickDurations().values(), (std::vector<uint32_t>{5, 6, 7}));
  EXPECT_EQ(stats.tickDurations().capacity(), net::maxServerStatsTicks);

  message.tickMicros.assign(net::maxServerStatsTicks, 9);
  stats.apply(message);
  EXPECT_EQ(stats.tickDurations().size(), net::maxServerStatsTicks);
  EXPECT_EQ(stats.tickDurations().totalPushed(), 3u + net::maxServerStatsTicks);
}

TEST_F(RemoteStatsTest, TrafficBatchesRecordTimeAndAreCapped)
{
  stats.subscribed();
  auto message = withSample(300, 0);
  message.samples.push_back({200, 0, 0, {}});
  message.out = {{static_cast<uint8_t>(net::MessageType::stateDelta), 4, 80}};
  message.in = {{static_cast<uint8_t>(net::MessageType::inputState), 2, 20}};
  stats.apply(message);

  ASSERT_EQ(stats.trafficBatches().size(), 1u);
  const auto& batch = stats.trafficBatches().front();
  EXPECT_EQ(batch.arrival, now);
  EXPECT_EQ(batch.wallMicros, 500u);
  ASSERT_EQ(batch.out.size(), 1u);
  EXPECT_EQ(batch.out[0].bytes, 80u);
  ASSERT_EQ(batch.in.size(), 1u);
  EXPECT_EQ(batch.in[0].messages, 2u);

  now += 1s;
  stats.apply(net::ServerStats{});
  ASSERT_EQ(stats.trafficBatches().size(), 2u);
  EXPECT_TRUE(stats.trafficBatches().back().out.empty());
  EXPECT_EQ(stats.trafficBatches().back().wallMicros, 0u);
  EXPECT_EQ(stats.trafficBatches().back().arrival, now);

  for (std::size_t i = 0; i < net::RemoteStats::trafficBatchCapacity + 5; ++i)
  {
    now += 1s;
    stats.apply(net::ServerStats{});
  }

  ASSERT_EQ(stats.trafficBatches().size(), net::RemoteStats::trafficBatchCapacity);
  EXPECT_EQ(stats.trafficBatches().back().arrival, now);
  EXPECT_EQ(stats.trafficBatches().front().arrival,
            now - std::chrono::seconds(net::RemoteStats::trafficBatchCapacity - 1));
}

TEST_F(RemoteStatsTest, SnapshotsAreDatedAndCapped)
{
  stats.subscribed();
  net::ServerStats message;
  message.snapshots = {{1500, 4096, net::SnapshotReason::join, 800, 1},
                       {20, 8192, net::SnapshotReason::scriptReload, 1200, 3}};
  stats.apply(message);

  ASSERT_EQ(stats.snapshots().size(), 2u);
  EXPECT_EQ(stats.snapshots()[0].takenAt, now - 1500ms);
  EXPECT_EQ(stats.snapshots()[0].record.bytes, 4096u);
  EXPECT_EQ(stats.snapshots()[1].takenAt, now - 20ms);
  EXPECT_EQ(stats.snapshots()[1].record.reason, net::SnapshotReason::scriptReload);

  message.snapshots.clear();
  for (uint32_t i = 0; i < net::maxServerStatsSnapshots; ++i)
  {
    message.snapshots.push_back({0, 1000 + i, net::SnapshotReason::join, 0, 1});
  }
  stats.apply(message);

  ASSERT_EQ(stats.snapshots().size(), net::maxServerStatsSnapshots);
  EXPECT_EQ(stats.snapshots().front().record.bytes, 1000u);
  EXPECT_EQ(stats.snapshots().back().record.bytes, 1000u + net::maxServerStatsSnapshots - 1);
}

TEST_F(RemoteStatsTest, ScalarsFollowTheLatestMessage)
{
  stats.subscribed();
  net::ServerStats message;
  message.flags = net::serverStatsFlags::editMode;
  message.connections = 3;
  message.editors = 2;
  message.objects = 85;
  stats.apply(message);

  EXPECT_EQ(stats.flags(), net::serverStatsFlags::editMode);
  EXPECT_EQ(stats.connections(), 3u);
  EXPECT_EQ(stats.editors(), 2u);
  EXPECT_EQ(stats.objects(), 85u);
}

TEST_F(RemoteStatsTest, AnotherVersionIsUnsupportedWithoutThrowing)
{
  stats.subscribed();
  auto message = table();
  message.samples = {{100, 1, 0, {{1, 5, 1}}}};
  const auto packed = net::packServerStats(message, true);

  stats.apply(withVersion(packed, net::serverStatsVersion));
  ASSERT_EQ(stats.history().size(), 1u);
  EXPECT_EQ(stats.state(), State::live);

  EXPECT_NO_THROW(stats.apply(withVersion(packed, 2)));
  EXPECT_EQ(stats.state(), State::unsupported);
  EXPECT_EQ(stats.history().size(), 1u);
  EXPECT_EQ(stats.nodeCount(), 3u);

  now += 10s;
  EXPECT_EQ(stats.state(), State::unsupported);
}

TEST_F(RemoteStatsTest, PackedMessagesApplyAndTruncatedOnesThrow)
{
  stats.subscribed();
  auto message = table();
  message.samples = {{100, 1, 0, {{2, 5, 1}}}};
  const auto packed = net::packServerStats(message, true);

  stats.apply(packed);
  EXPECT_EQ(stats.nodeName(2), "Physics");
  EXPECT_EQ(stats.history().newest().nodeMicros[2], 5u);

  const auto bytes = packed.bytes();
  const net::Message truncated(net::MessageType::serverStats, bytes.first(bytes.size() - 1));
  EXPECT_THROW(stats.apply(truncated), std::runtime_error);

  const net::Message tiny(net::MessageType::serverStats, bytes.first(1));
  EXPECT_THROW(stats.apply(tiny), std::runtime_error);
  EXPECT_EQ(stats.history().size(), 1u);
}
