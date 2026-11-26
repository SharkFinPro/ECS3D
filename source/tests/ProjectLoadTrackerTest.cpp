#include "ProjectLoadTracker.h"
#include <gtest/gtest.h>
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainUpload.h"
#include "terrain/TerrainVolume.h"
#include <memory>
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

TEST(ProjectLoadTracker, AwaitingOwnLoadLastsUntilItsSnapshotArrives)
{
  ProjectLoadTracker tracker;
  ASSERT_FALSE(tracker.observeSnapshot(a));
  EXPECT_FALSE(tracker.awaitingOwnLoad());

  tracker.expectOwnLoad(b);
  EXPECT_TRUE(tracker.awaitingOwnLoad());
  EXPECT_FALSE(tracker.observeSnapshot(a));
  EXPECT_TRUE(tracker.awaitingOwnLoad());
  EXPECT_FALSE(tracker.observeSnapshot(b));
  EXPECT_FALSE(tracker.awaitingOwnLoad());
}

TEST(ProjectLoadTracker, AnOwnLoadBeforeAnySnapshotIsAdoptedWhenItArrives)
{
  ProjectLoadTracker tracker;

  tracker.expectOwnLoad(x);
  EXPECT_FALSE(tracker.observeSnapshot(y));
  EXPECT_FALSE(tracker.observeSnapshot(x));
  EXPECT_TRUE(tracker.observeSnapshot(y));
}

TEST(ProjectLoadTracker, OnlyTheSnapshotAnsweringAnAwaitedOwnLoadReflectsTheServer)
{
  ProjectLoadTracker tracker;
  EXPECT_TRUE(tracker.snapshotIsCurrent(a));

  tracker.expectOwnLoad(b);
  EXPECT_FALSE(tracker.snapshotIsCurrent(a));
  EXPECT_FALSE(tracker.snapshotIsCurrent(c));
  EXPECT_TRUE(tracker.snapshotIsCurrent(b));

  ASSERT_FALSE(tracker.observeSnapshot(b));
  EXPECT_TRUE(tracker.snapshotIsCurrent(a));
}

TEST(ProjectLoadTracker, AnOpenProjectRaceKeepsTheNewProjectsUploadsUntilItsOwnSnapshotLacksThem)
{
  const auto terrainUuid = idFrom("11111111-1111-4111-8111-111111111111");
  auto volume = std::make_shared<terrain::TerrainVolume>(2.5f);
  volume->setChunk({ 0, 0, 0 }, terrain::TerrainChunk::uniform(-terrain::airDistance, 1));

  terrain::TerrainUpload upload;
  upload.add(terrainUuid, volume);

  // The store still reflects the old project, which has no such terrain.
  terrain::TerrainStore store;
  ProjectLoadTracker tracker;
  tracker.expectOwnLoad(b);

  // A snapshot already in flight from before the server applied the load: dropping on it would lose the upload.
  const bool staleMayDrop = tracker.snapshotIsCurrent(a);
  if (staleMayDrop)
  {
    static_cast<void>(terrain::forgetUnknownTerrains(upload, store));
  }
  EXPECT_FALSE(staleMayDrop);
  EXPECT_EQ(upload.terrains().size(), 1u);

  // The control: the same snapshot, once the wait is over, does drop a terrain the store lacks.
  ASSERT_FALSE(tracker.observeSnapshot(b));
  ASSERT_TRUE(tracker.snapshotIsCurrent(a));
  EXPECT_EQ(terrain::forgetUnknownTerrains(upload, store).size(), 1u);
  EXPECT_TRUE(upload.terrains().empty());

  // And the answering snapshot keeps a terrain that it does hold.
  upload.add(terrainUuid, volume);
  ASSERT_NE(store.create({ .uuid = terrainUuid }), nullptr);
  EXPECT_TRUE(terrain::forgetUnknownTerrains(upload, store).empty());
}
