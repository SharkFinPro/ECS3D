#include <gtest/gtest.h>

#include <LiveObjectTracker.h>

#include <cstdio>
#include <string>
#include <unordered_set>
#include <vector>

namespace {
  uuids::uuid uuidN(const int n)
  {
    char text[40];
    std::snprintf(text, sizeof(text), "%08x-0000-4000-8000-%012x", n * 7919 + 1, n);
    return uuids::uuid::from_string(std::string(text)).value();
  }

  bool runFrame(LiveObjectTracker& tracker, const std::vector<int>& ids)
  {
    tracker.beginFrame();
    for (const int id : ids)
    {
      tracker.add(uuidN(id));
    }

    return tracker.endFrame();
  }

  std::unordered_set<uuids::uuid> setOf(const std::vector<int>& ids)
  {
    std::unordered_set<uuids::uuid> result;
    for (const int id : ids)
    {
      result.insert(uuidN(id));
    }

    return result;
  }
}

TEST(LiveObjectTracker, TheFirstFrameReportsChanged)
{
  LiveObjectTracker tracker;

  EXPECT_TRUE(runFrame(tracker, {1, 2, 3}));
  EXPECT_EQ(tracker.liveUUIDs(), setOf({1, 2, 3}));
}

TEST(LiveObjectTracker, TheSameUuidsInAnotherOrderReportUnchanged)
{
  LiveObjectTracker tracker;
  ASSERT_TRUE(runFrame(tracker, {1, 2, 3}));

  EXPECT_FALSE(runFrame(tracker, {3, 1, 2}));
  EXPECT_EQ(tracker.liveUUIDs(), setOf({1, 2, 3}));
}

TEST(LiveObjectTracker, AddingAUuidReportsChanged)
{
  LiveObjectTracker tracker;
  ASSERT_TRUE(runFrame(tracker, {1, 2, 3}));

  EXPECT_TRUE(runFrame(tracker, {1, 2, 3, 4}));
  EXPECT_EQ(tracker.liveUUIDs(), setOf({1, 2, 3, 4}));
}

TEST(LiveObjectTracker, RemovingAUuidReportsChanged)
{
  LiveObjectTracker tracker;
  ASSERT_TRUE(runFrame(tracker, {1, 2, 3}));

  EXPECT_TRUE(runFrame(tracker, {1, 3}));
  EXPECT_EQ(tracker.liveUUIDs(), setOf({1, 3}));
}

TEST(LiveObjectTracker, SwappingAUuidForANewOneReportsChanged)
{
  LiveObjectTracker tracker;
  ASSERT_TRUE(runFrame(tracker, {1, 2, 3}));

  EXPECT_TRUE(runFrame(tracker, {1, 2, 9}));
  EXPECT_EQ(tracker.liveUUIDs(), setOf({1, 2, 9}));
}

TEST(LiveObjectTracker, AChangeIsReportedOnceThenSettles)
{
  LiveObjectTracker tracker;
  ASSERT_TRUE(runFrame(tracker, {1, 2}));
  ASSERT_TRUE(runFrame(tracker, {1, 2, 3}));

  EXPECT_FALSE(runFrame(tracker, {1, 2, 3}));
  EXPECT_EQ(tracker.liveUUIDs(), setOf({1, 2, 3}));
}

TEST(LiveObjectTracker, AnEmptyFrameAfterANonEmptyOneReportsChangedAndEmptiesTheSet)
{
  LiveObjectTracker tracker;
  ASSERT_TRUE(runFrame(tracker, {1, 2}));
  ASSERT_EQ(tracker.liveUUIDs().size(), 2u);

  EXPECT_TRUE(runFrame(tracker, {}));
  EXPECT_TRUE(tracker.liveUUIDs().empty());
}

TEST(LiveObjectTracker, TwoEmptyFramesInARowReportUnchangedAfterTheFirst)
{
  LiveObjectTracker tracker;

  EXPECT_TRUE(runFrame(tracker, {}));
  EXPECT_FALSE(runFrame(tracker, {}));
}
