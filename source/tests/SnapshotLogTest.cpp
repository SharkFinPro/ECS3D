#include <gtest/gtest.h>

#include <SnapshotLog.h>

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>

namespace {
  net::SnapshotRecord recordWithBytes(const uint32_t bytes)
  {
    net::SnapshotRecord record;
    record.bytes = bytes;
    return record;
  }
}

TEST(SnapshotLog, StartsEmpty)
{
  const net::SnapshotLog log;

  EXPECT_EQ(log.totalRecorded(), 0u);
  EXPECT_EQ(log.size(), 0u);
}

TEST(SnapshotLog, KeepsEntriesOldestFirstAndCountsThem)
{
  net::SnapshotLog log;

  log.record({net::SnapshotReason::join, 100, 7, 1});
  log.record({net::SnapshotReason::sceneEdit, 200, 8, 3});

  ASSERT_EQ(log.size(), 2u);
  EXPECT_EQ(log.totalRecorded(), 2u);
  EXPECT_EQ(log.at(0).reason, net::SnapshotReason::join);
  EXPECT_EQ(log.at(0).buildMicros, 7u);
  EXPECT_EQ(log.at(1).reason, net::SnapshotReason::sceneEdit);
  EXPECT_EQ(log.at(1).recipients, 3u);
}

TEST(SnapshotLog, WrapsKeepingTheNewestWhileTheTotalKeepsGrowing)
{
  net::SnapshotLog log;
  const auto count = net::SnapshotLog::capacity + 5;

  for (uint32_t i = 1; i <= count; ++i)
  {
    log.record(recordWithBytes(i));
  }

  EXPECT_EQ(log.totalRecorded(), count);
  ASSERT_EQ(log.size(), net::SnapshotLog::capacity);
  EXPECT_EQ(log.at(0).bytes, 6u);
  EXPECT_EQ(log.at(log.size() - 1).bytes, count);
}

TEST(SnapshotLog, EveryReasonHasADistinctName)
{
  std::set<std::string> names;
  for (uint8_t i = 0; i <= static_cast<uint8_t>(net::SnapshotReason::other); ++i)
  {
    const auto name = net::snapshotReasonName(static_cast<net::SnapshotReason>(i));
    EXPECT_NE(name, "unknown") << static_cast<int>(i);
    names.insert(std::string(name));
  }

  EXPECT_EQ(names.size(), static_cast<std::size_t>(net::SnapshotReason::other) + 1);
  EXPECT_EQ(net::snapshotReasonName(net::SnapshotReason::editRefusedResync), "editRefusedResync");
  EXPECT_EQ(net::snapshotReasonName(static_cast<net::SnapshotReason>(99)), "unknown");
}
