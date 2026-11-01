#include <gtest/gtest.h>

#include "TestScene.h"
#include "edits/EditCommand.h"
#include "edits/PlaySessionHistory.h"
#include "scenes/SceneManager.h"

#include <cstddef>
#include <string>
#include <uuid.h>

namespace {
  // The name tells one recorded edit from another, so a test can say which history is current.
  void recordEdit(edits::PlaySessionHistory& history, const std::string& name = "Edit")
  {
    const auto object = uuids::uuid::from_string("11111111-1111-4111-8111-111111111111").value();
    history.current().record(edits::EditCommand::renameObject(object, "Before", name));
  }

  std::string topLabel(const edits::PlaySessionHistory& history)
  {
    const auto scene = fixtures::makeScene();
    return history.current().nextUndoLabel(*scene.objectManager).value_or("");
  }

  std::size_t depth(const edits::PlaySessionHistory& history)
  {
    return history.current().undoDepth();
  }

  // A history whose last report was stopped, holding one authored edit.
  edits::PlaySessionHistory stoppedWithOneEdit()
  {
    edits::PlaySessionHistory history;
    EXPECT_FALSE(history.observeStatus(SceneStatus::stopped));
    recordEdit(history, "Authored");
    return history;
  }
}

TEST(PlaySessionHistoryTest, EditSentAfterStartStaysOutOfTheAuthoredHistory)
{
  auto history = stoppedWithOneEdit();

  EXPECT_TRUE(history.requestStart());
  EXPECT_TRUE(history.hasStash());
  EXPECT_EQ(depth(history), 0u);

  recordEdit(history);
  EXPECT_FALSE(history.observeStatus(SceneStatus::running));
  recordEdit(history);
  EXPECT_EQ(depth(history), 2u);

  EXPECT_TRUE(history.requestStop());
  EXPECT_FALSE(history.hasStash());
  EXPECT_EQ(depth(history), 1u);
}

TEST(PlaySessionHistoryTest, StaleStoppedAfterStartChangesNothingAndRunningDoesNotStashAgain)
{
  auto history = stoppedWithOneEdit();
  ASSERT_TRUE(history.requestStart());
  recordEdit(history);

  EXPECT_FALSE(history.observeStatus(SceneStatus::stopped));
  EXPECT_TRUE(history.hasStash());
  EXPECT_EQ(depth(history), 1u);

  EXPECT_FALSE(history.observeStatus(SceneStatus::running));
  EXPECT_TRUE(history.hasStash());
  EXPECT_EQ(depth(history), 1u);

  EXPECT_TRUE(history.requestStop());
  EXPECT_EQ(depth(history), 1u);
}

TEST(PlaySessionHistoryTest, StopRestoresAtOnceAndStaleRunningDoesNotStashAgain)
{
  auto history = stoppedWithOneEdit();
  ASSERT_TRUE(history.requestStart());
  ASSERT_FALSE(history.observeStatus(SceneStatus::running));
  recordEdit(history);

  EXPECT_TRUE(history.requestStop());
  EXPECT_EQ(depth(history), 1u);

  recordEdit(history);
  EXPECT_FALSE(history.observeStatus(SceneStatus::running));
  EXPECT_FALSE(history.hasStash());
  EXPECT_EQ(depth(history), 2u);

  EXPECT_FALSE(history.observeStatus(SceneStatus::stopped));
  EXPECT_FALSE(history.hasStash());
  EXPECT_EQ(depth(history), 2u);
}

TEST(PlaySessionHistoryTest, StartThenStopBeforeAnyStatusLeavesTheAuthoredHistoryIntact)
{
  auto history = stoppedWithOneEdit();

  ASSERT_TRUE(history.requestStart());
  recordEdit(history);
  recordEdit(history);
  ASSERT_TRUE(history.requestStop());
  EXPECT_EQ(depth(history), 1u);

  EXPECT_FALSE(history.observeStatus(SceneStatus::running));
  EXPECT_FALSE(history.observeStatus(SceneStatus::stopped));
  EXPECT_FALSE(history.hasStash());
  EXPECT_EQ(depth(history), 1u);
}

TEST(PlaySessionHistoryTest, AnotherEditorsStartAndStopAreFollowedFromTheStatusAlone)
{
  edits::PlaySessionHistory history;
  EXPECT_FALSE(history.observeStatus(SceneStatus::stopped));
  recordEdit(history);

  EXPECT_TRUE(history.observeStatus(SceneStatus::running));
  EXPECT_TRUE(history.hasStash());
  EXPECT_EQ(depth(history), 0u);

  recordEdit(history);
  recordEdit(history);

  EXPECT_FALSE(history.observeStatus(SceneStatus::paused));
  EXPECT_FALSE(history.observeStatus(SceneStatus::running));
  EXPECT_EQ(depth(history), 2u);

  EXPECT_TRUE(history.observeStatus(SceneStatus::stopped));
  EXPECT_FALSE(history.hasStash());
  EXPECT_EQ(depth(history), 1u);
}

TEST(PlaySessionHistoryTest, FirstReportEverIsNotATransition)
{
  edits::PlaySessionHistory history;
  recordEdit(history);

  EXPECT_FALSE(history.observeStatus(SceneStatus::running));
  EXPECT_FALSE(history.hasStash());
  EXPECT_EQ(depth(history), 1u);
}

TEST(PlaySessionHistoryTest, StartWhilePausedIsAResumeAndDoesNotSwap)
{
  auto history = stoppedWithOneEdit();
  ASSERT_TRUE(history.requestStart());
  ASSERT_FALSE(history.observeStatus(SceneStatus::running));
  ASSERT_FALSE(history.observeStatus(SceneStatus::paused));
  recordEdit(history, "Play");

  EXPECT_FALSE(history.requestStart());
  EXPECT_TRUE(history.hasStash());
  EXPECT_EQ(depth(history), 1u);
  EXPECT_EQ(topLabel(history), "Rename Play");

  EXPECT_FALSE(history.observeStatus(SceneStatus::running));
  EXPECT_EQ(depth(history), 1u);
  EXPECT_TRUE(history.requestStop());
  EXPECT_EQ(depth(history), 1u);
  EXPECT_EQ(topLabel(history), "Rename Authored");
}

TEST(PlaySessionHistoryTest, ClearDropsBothHistoriesAndTheExpectation)
{
  auto history = stoppedWithOneEdit();
  ASSERT_TRUE(history.requestStart());
  recordEdit(history);

  history.clear();
  EXPECT_FALSE(history.hasStash());
  EXPECT_EQ(depth(history), 0u);

  recordEdit(history);
  EXPECT_TRUE(history.observeStatus(SceneStatus::running));
  EXPECT_TRUE(history.hasStash());
  EXPECT_EQ(depth(history), 0u);
}

TEST(PlaySessionHistoryTest, StaleStatusCannotConfirmALaterRequest)
{
  auto history = stoppedWithOneEdit();
  ASSERT_TRUE(history.requestStart());
  ASSERT_FALSE(history.observeStatus(SceneStatus::running));
  ASSERT_FALSE(history.observeStatus(SceneStatus::paused));
  recordEdit(history);

  ASSERT_TRUE(history.requestStop());
  ASSERT_TRUE(history.requestStart());
  recordEdit(history, "SecondPlay");

  EXPECT_FALSE(history.observeStatus(SceneStatus::paused));
  EXPECT_FALSE(history.observeStatus(SceneStatus::stopped));
  EXPECT_FALSE(history.observeStatus(SceneStatus::running));

  EXPECT_TRUE(history.hasStash());
  EXPECT_EQ(depth(history), 1u);
  EXPECT_EQ(topLabel(history), "Rename SecondPlay");
  EXPECT_TRUE(history.requestStop());
  EXPECT_EQ(depth(history), 1u);
  EXPECT_EQ(topLabel(history), "Rename Authored");
}

TEST(PlaySessionHistoryTest, ResetMakesTheNextReportAFirstReport)
{
  auto history = stoppedWithOneEdit();
  history.reset();
  recordEdit(history);

  EXPECT_FALSE(history.observeStatus(SceneStatus::running));
  EXPECT_FALSE(history.hasStash());
  EXPECT_EQ(depth(history), 1u);

  auto control = stoppedWithOneEdit();
  control.clear();
  recordEdit(control);

  EXPECT_TRUE(control.observeStatus(SceneStatus::running));
  EXPECT_TRUE(control.hasStash());
}

TEST(PlaySessionHistoryTest, StartIsRefusedWhenTheServerIsNotKnownToBeStopped)
{
  edits::PlaySessionHistory history;
  ASSERT_FALSE(history.observeStatus(SceneStatus::running));
  recordEdit(history);

  EXPECT_FALSE(history.requestStart());
  EXPECT_FALSE(history.hasStash());
  EXPECT_EQ(depth(history), 1u);

  // Positive control: once the server reports stopped, the same request starts a session.
  ASSERT_TRUE(history.observeStatus(SceneStatus::stopped));
  EXPECT_TRUE(history.requestStart());
  EXPECT_TRUE(history.hasStash());

  // No report at all is not "stopped" either.
  edits::PlaySessionHistory fresh;
  EXPECT_FALSE(fresh.requestStart());
  EXPECT_FALSE(fresh.hasStash());
}

TEST(PlaySessionHistoryTest, StopWithNothingStashedChangesNothing)
{
  edits::PlaySessionHistory fresh;
  EXPECT_FALSE(fresh.requestStop());

  auto history = stoppedWithOneEdit();
  EXPECT_FALSE(history.requestStop());
  EXPECT_EQ(depth(history), 1u);
  EXPECT_EQ(topLabel(history), "Rename Authored");

  // A stop that already restored the stash has nothing left to restore.
  ASSERT_TRUE(history.requestStart());
  ASSERT_TRUE(history.requestStop());
  EXPECT_FALSE(history.requestStop());
  EXPECT_EQ(depth(history), 1u);
  EXPECT_EQ(topLabel(history), "Rename Authored");
}
