#include "ProjectLoadTracker.h"
#include <gtest/gtest.h>
#include <uuid.h>
#include <string>

namespace {
  uuids::uuid idFrom(const std::string& text)
  {
    return uuids::uuid::from_string(text).value();
  }

  const auto a = idFrom("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa");
  const auto b = idFrom("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb");
  const auto c = idFrom("cccccccc-cccc-cccc-cccc-cccccccccccc");
  const auto d = idFrom("dddddddd-dddd-dddd-dddd-dddddddddddd");
  const auto x = idFrom("eeeeeeee-eeee-eeee-eeee-eeeeeeeeeeee");
  const auto y = idFrom("ffffffff-ffff-ffff-ffff-ffffffffffff");
}

TEST(ProjectLoadTracker, TheFirstSnapshotIsAdopted)
{
  ProjectLoadTracker tracker;

  EXPECT_FALSE(tracker.observeSnapshot(a));
  EXPECT_FALSE(tracker.observeSnapshot(a));
}

TEST(ProjectLoadTracker, ADifferentIdIsReportedExactlyOnce)
{
  ProjectLoadTracker tracker;
  ASSERT_FALSE(tracker.observeSnapshot(a));

  EXPECT_TRUE(tracker.observeSnapshot(b));
  EXPECT_FALSE(tracker.observeSnapshot(b));
  EXPECT_TRUE(tracker.observeSnapshot(a));
}

TEST(ProjectLoadTracker, AnOldSnapshotDoesNotCancelAPendingOwnLoad)
{
  ProjectLoadTracker tracker;
  ASSERT_FALSE(tracker.observeSnapshot(a));

  tracker.expectOwnLoad(b);
  EXPECT_FALSE(tracker.observeSnapshot(a));
  EXPECT_FALSE(tracker.observeSnapshot(b));
  EXPECT_TRUE(tracker.observeSnapshot(c));
}

TEST(ProjectLoadTracker, AnotherEditorsLoadWhileOwnIsPendingIsNotReportedUntilLater)
{
  ProjectLoadTracker tracker;
  ASSERT_FALSE(tracker.observeSnapshot(a));

  tracker.expectOwnLoad(b);
  EXPECT_FALSE(tracker.observeSnapshot(c));
  EXPECT_FALSE(tracker.observeSnapshot(b));
  EXPECT_TRUE(tracker.observeSnapshot(c));
}

TEST(ProjectLoadTracker, ASecondOwnLoadReplacesTheFirst)
{
  ProjectLoadTracker tracker;
  ASSERT_FALSE(tracker.observeSnapshot(a));

  tracker.expectOwnLoad(b);
  tracker.expectOwnLoad(c);
  EXPECT_FALSE(tracker.observeSnapshot(b));
  EXPECT_FALSE(tracker.observeSnapshot(c));
  EXPECT_TRUE(tracker.observeSnapshot(b));
}

TEST(ProjectLoadTracker, ResetDropsAPendingOwnLoad)
{
  ProjectLoadTracker tracker;
  ASSERT_FALSE(tracker.observeSnapshot(a));

  tracker.expectOwnLoad(b);
  tracker.reset();
  EXPECT_FALSE(tracker.observeSnapshot(c));
  EXPECT_TRUE(tracker.observeSnapshot(d));
}

TEST(ProjectLoadTracker, AnOwnLoadBeforeAnySnapshotIsAdoptedWhenItArrives)
{
  ProjectLoadTracker tracker;

  tracker.expectOwnLoad(x);
  EXPECT_FALSE(tracker.observeSnapshot(y));
  EXPECT_FALSE(tracker.observeSnapshot(x));
  EXPECT_TRUE(tracker.observeSnapshot(y));
}
