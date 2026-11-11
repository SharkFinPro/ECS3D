#include <gtest/gtest.h>

#include "EditHistoryFixtures.h"
#include "edits/PlaySessionHistory.h"
#include <UnsavedChanges.h>

#include <cstddef>
#include <cstdint>

namespace {
  using namespace editHistoryFixtures;

  // A scene, its history and the unsaved-changes tracker, wired the way the editor wires them: the
  // tracker reads the history's position, and a recorded edit does not call markEdited.
  struct Project {
    Scene scene = makeScene();
    edits::EditHistory history;
    UnsavedChanges changes;
    float x = 0.0f;

    [[nodiscard]] bool dirty() const
    {
      return changes.isDirty(history.position());
    }

    void save()
    {
      changes.markSaved(history.position());
    }

    void edit()
    {
      const auto transform = transformOf(scene.object);
      const auto before = transform->serialize();
      x += 1.0f;
      transform->setPosition({ x, 0, 0 });
      history.record(edits::EditCommand::componentEdit(scene.object->getUUID(), before, transform->serialize()));
    }

    bool undo()
    {
      const auto outcome = history.undo(*scene.objectManager);
      if (!outcome.ok())
      {
        return false;
      }

      replication::applyComponentEdit(*scene.objectManager, *outcome.messagePayload);
      return true;
    }

    bool redo()
    {
      const auto outcome = history.redo(*scene.objectManager);
      if (!outcome.ok())
      {
        return false;
      }

      replication::applyComponentEdit(*scene.objectManager, *outcome.messagePayload);
      return true;
    }
  };
}

TEST(UnsavedChanges, StartsClean)
{
  const UnsavedChanges changes;

  EXPECT_FALSE(changes.isDirty(0));
}

TEST(UnsavedChanges, AnEditMakesItDirtyAndASaveMakesItCleanAgain)
{
  UnsavedChanges changes;

  changes.markEdited();
  EXPECT_TRUE(changes.isDirty(0));

  changes.markSaved(0);
  EXPECT_FALSE(changes.isDirty(0));
}

TEST(UnsavedChanges, AnEditAfterASaveIsDirtyAgain)
{
  UnsavedChanges changes;
  changes.markEdited();
  changes.markSaved(0);
  ASSERT_FALSE(changes.isDirty(0));

  changes.markEdited();

  EXPECT_TRUE(changes.isDirty(0));
}

TEST(UnsavedChanges, SeveralEditsNeedOnlyOneSave)
{
  UnsavedChanges changes;
  changes.markEdited();
  changes.markEdited();
  changes.markEdited();
  ASSERT_TRUE(changes.isDirty(0));

  changes.markSaved(0);

  EXPECT_FALSE(changes.isDirty(0));
}

TEST(UnsavedChanges, SavingACleanProjectLeavesItClean)
{
  UnsavedChanges changes;

  changes.markSaved(0);

  EXPECT_FALSE(changes.isDirty(0));
}

TEST(DecideDiscard, CoversEveryCombination)
{
  EXPECT_EQ(decideDiscard(false, false), DiscardDecision::performNow);
  EXPECT_EQ(decideDiscard(false, true), DiscardDecision::performNow);
  EXPECT_EQ(decideDiscard(true, false), DiscardDecision::prompt);
  EXPECT_EQ(decideDiscard(true, true), DiscardDecision::ignore);
}

TEST(UnsavedChanges, UndoingBackToTheSavePointIsClean)
{
  Project project;
  project.save();

  project.edit();
  ASSERT_TRUE(project.dirty());

  ASSERT_TRUE(project.undo());

  EXPECT_FALSE(project.dirty());
}

TEST(UnsavedChanges, RedoingAfterUndoingToTheSavePointIsDirtyAgain)
{
  Project project;
  project.save();
  project.edit();
  ASSERT_TRUE(project.undo());
  ASSERT_FALSE(project.dirty());

  ASSERT_TRUE(project.redo());

  EXPECT_TRUE(project.dirty());
}

TEST(UnsavedChanges, UndoingPastASavedEditIsDirtyAndRedoingBackIsClean)
{
  Project project;
  project.edit();
  project.save();
  ASSERT_FALSE(project.dirty());

  ASSERT_TRUE(project.undo());
  EXPECT_TRUE(project.dirty());

  ASSERT_TRUE(project.redo());
  EXPECT_FALSE(project.dirty());
}

TEST(UnsavedChanges, ANewEditAfterUndoDoesNotMatchTheSavedDepth)
{
  Project project;
  project.edit();
  project.save();
  const auto savedDepth = project.history.undoDepth();

  ASSERT_TRUE(project.undo());
  project.edit();

  ASSERT_EQ(project.history.undoDepth(), savedDepth);
  EXPECT_TRUE(project.dirty());
}

TEST(UnsavedChanges, TrimmingPastTheSavePointKeepsItDirtyAllTheWayBack)
{
  Project project;
  project.save();

  for (std::size_t i = 0; i <= edits::EditHistory::maxDepth; ++i)
  {
    project.edit();
  }
  ASSERT_EQ(project.history.undoDepth(), edits::EditHistory::maxDepth);

  while (project.history.canUndo())
  {
    ASSERT_TRUE(project.undo());
  }

  EXPECT_TRUE(project.dirty());
}

TEST(UnsavedChanges, TrimmingKeepsASavePointThatIsStillOnTheStack)
{
  Project project;
  project.edit();
  project.edit();
  project.save();

  for (std::size_t i = 0; i < edits::EditHistory::maxDepth - 2; ++i)
  {
    project.edit();
  }
  ASSERT_EQ(project.history.undoDepth(), edits::EditHistory::maxDepth);
  project.edit();
  ASSERT_EQ(project.history.undoDepth(), edits::EditHistory::maxDepth);

  for (std::size_t i = 0; i < edits::EditHistory::maxDepth - 1; ++i)
  {
    ASSERT_TRUE(project.undo());
  }

  EXPECT_FALSE(project.dirty());
}

TEST(UnsavedChanges, ClearingAHistoryHoldingTheSavePointKeepsItDirty)
{
  Project project;
  project.edit();
  project.save();

  project.history.clear();

  EXPECT_TRUE(project.dirty());
}

TEST(UnsavedChanges, ClearingAnEmptyHistoryStaysClean)
{
  Project project;
  project.save();

  project.history.clear();

  EXPECT_FALSE(project.dirty());
}

TEST(UnsavedChanges, ARefusedUndoNeverReadsClean)
{
  Project project;
  project.save();
  project.edit();

  transformOf(project.scene.object)->setPosition({ 99, 0, 0 });
  const auto outcome = project.history.undo(*project.scene.objectManager);
  ASSERT_FALSE(outcome.ok());
  ASSERT_FALSE(project.history.canUndo());

  EXPECT_TRUE(project.dirty());
}

TEST(UnsavedChanges, AnEditOutsideTheHistoryOutlivesUndoingAnotherEdit)
{
  Project project;
  project.save();

  project.edit();
  project.changes.markEdited();
  ASSERT_TRUE(project.undo());

  EXPECT_TRUE(project.dirty());

  project.save();
  EXPECT_FALSE(project.dirty());
}

TEST(UnsavedChanges, AnUncommittedEditKeepsItDirtyUntilItCommits)
{
  Project project;
  project.save();

  project.changes.beginUncommittedEdit();
  EXPECT_TRUE(project.dirty());

  project.changes.endUncommittedEdit();
  EXPECT_FALSE(project.dirty());
}

TEST(UnsavedChanges, ASaveAfterLoadingStartsFromTheNewHistory)
{
  Project project;
  project.edit();
  project.changes.markEdited();

  project.history.clear();
  project.save();

  EXPECT_FALSE(project.dirty());

  project.edit();
  EXPECT_TRUE(project.dirty());
  ASSERT_TRUE(project.undo());
  EXPECT_FALSE(project.dirty());
}

TEST(UnsavedChanges, APlaySessionHistoryNeverMovesTheAuthoredPosition)
{
  Project project;
  edits::PlaySessionHistory sessions;
  sessions.observeStatus(SceneStatus::stopped);
  const auto saved = sessions.authoredPosition();

  ASSERT_TRUE(sessions.requestStart());
  const auto transform = transformOf(project.scene.object);
  const auto before = transform->serialize();
  transform->setPosition({ 5, 0, 0 });
  sessions.current().record(
    edits::EditCommand::componentEdit(project.scene.object->getUUID(), before, transform->serialize()));

  EXPECT_EQ(sessions.authoredPosition(), saved);
  EXPECT_NE(sessions.current().position(), saved);

  ASSERT_TRUE(sessions.requestStop());
  EXPECT_EQ(sessions.authoredPosition(), saved);
  EXPECT_EQ(sessions.current().position(), saved);
}
