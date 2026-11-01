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
  ProjectIdentityCheck check;
  check.setOwnProject(0, false);

  EXPECT_FALSE(check.observeSnapshot(otherProject));

  // Positive control: the same snapshot is reported once the editor does hold a project.
  check.setOwnProject(ownProject, false);
  check.reset();
  EXPECT_TRUE(check.observeSnapshot(otherProject));
}

TEST(ProjectIdentityCheck, ASpawnedServerIsTrustedEvenWhenTheFingerprintsDiffer)
{
  ProjectIdentityCheck check;
  check.setOwnProject(ownProject, true);

  EXPECT_FALSE(check.observeSnapshot(otherProject));

  // Positive control: the same snapshot is reported once the server is no longer spawned by the editor.
  check.setOwnProject(ownProject, false);
  check.reset();
  EXPECT_TRUE(check.observeSnapshot(otherProject));
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
