#include "ProjectIdentityCheck.h"
#include <gtest/gtest.h>
#include <cstdint>

namespace {
  constexpr uint64_t ownProject = 1111;
  constexpr uint64_t otherProject = 2222;
}

TEST(ProjectIdentityCheck, ARemoteServerHoldingTheSameProjectIsTrusted)
{
  ProjectIdentityCheck check;
  check.setOwnProject(ownProject, false);

  EXPECT_FALSE(check.observeSnapshot(ownProject));
}

TEST(ProjectIdentityCheck, ARemoteServerHoldingAnotherProjectIsReportedOnce)
{
  ProjectIdentityCheck check;
  check.setOwnProject(ownProject, false);

  EXPECT_TRUE(check.observeSnapshot(otherProject));
  EXPECT_FALSE(check.observeSnapshot(otherProject));
  EXPECT_FALSE(check.observeSnapshot(ownProject));
}

TEST(ProjectIdentityCheck, ARemoteServerOnTheBuiltInProjectIsAMismatch)
{
  ProjectIdentityCheck check;
  check.setOwnProject(ownProject, false);

  EXPECT_TRUE(check.observeSnapshot(0));
}

TEST(ProjectIdentityCheck, AnEditorWithNoProjectHasNothingToForget)
{
  ProjectIdentityCheck positive;
  positive.setOwnProject(ownProject, false);
  ASSERT_TRUE(positive.observeSnapshot(otherProject));

  ProjectIdentityCheck check;
  check.setOwnProject(0, false);

  EXPECT_FALSE(check.observeSnapshot(otherProject));
}

TEST(ProjectIdentityCheck, ASpawnedServerIsTrustedEvenWhenTheFingerprintsDiffer)
{
  ProjectIdentityCheck remote;
  remote.setOwnProject(ownProject, false);
  ASSERT_TRUE(remote.observeSnapshot(otherProject));

  ProjectIdentityCheck spawned;
  spawned.setOwnProject(ownProject, true);

  EXPECT_FALSE(spawned.observeSnapshot(otherProject));
}

TEST(ProjectIdentityCheck, OnlyTheFirstSnapshotOfAConnectionIsChecked)
{
  ProjectIdentityCheck check;
  check.setOwnProject(ownProject, false);
  ASSERT_FALSE(check.observeSnapshot(ownProject));

  EXPECT_FALSE(check.observeSnapshot(otherProject));
}

TEST(ProjectIdentityCheck, ANewConnectionIsCheckedAgain)
{
  ProjectIdentityCheck check;
  check.setOwnProject(ownProject, false);
  ASSERT_TRUE(check.observeSnapshot(otherProject));

  check.reset();

  EXPECT_TRUE(check.observeSnapshot(otherProject));
}
