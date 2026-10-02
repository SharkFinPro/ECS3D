#include <gtest/gtest.h>

#include "edits/EditCommand.h"
#include "edits/PlaySessionHistory.h"
#include "scenes/SceneManager.h"

#include <nlohmann/json.hpp>
#include <cstddef>
#include <uuid.h>

namespace {
  void recordEdit(edits::PlaySessionHistory& history)
  {
    const auto object = uuids::uuid::from_string("11111111-1111-4111-8111-111111111111").value();
    history.current().record(edits::EditCommand::componentEdit(object, nlohmann::json{{"x", 1}},
                                                                nlohmann::json{{"x", 2}}));
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
    recordEdit(history);
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
  recordEdit(history);

  EXPECT_FALSE(history.requestStart());
  EXPECT_TRUE(history.hasStash());
  EXPECT_EQ(depth(history), 1u);

  EXPECT_FALSE(history.observeStatus(SceneStatus::running));
  EXPECT_EQ(depth(history), 1u);
  EXPECT_TRUE(history.requestStop());
  EXPECT_EQ(depth(history), 1u);
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
  recordEdit(history);

  EXPECT_FALSE(history.observeStatus(SceneStatus::paused));
  EXPECT_FALSE(history.observeStatus(SceneStatus::stopped));
  EXPECT_FALSE(history.observeStatus(SceneStatus::running));

  EXPECT_TRUE(history.hasStash());
  EXPECT_EQ(depth(history), 1u);
  EXPECT_TRUE(history.requestStop());
  EXPECT_EQ(depth(history), 1u);
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
