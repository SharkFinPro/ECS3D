#include <gtest/gtest.h>

#include "TestScene.h"
#include "Replication.h"
#include "edits/EditCommand.h"
#include "edits/EditHistory.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Transform.h"
#include "EditHistoryFixtures.h"

#include <nlohmann/json.hpp>
#include <optional>

namespace {
  using namespace editHistoryFixtures;
}

// --- History bookkeeping.

TEST(EditHistory, RecordingANewCommandClearsTheRedoStack)
{
  auto scene = makeScene();
  const auto transform = transformOf(scene.object);

  transform->setPosition({ 1, 0, 0 });
  const auto before = transform->serialize();
  transform->setPosition({ 2, 0, 0 });
  const auto after = transform->serialize();

  edits::EditHistory history;
  history.record(edits::EditCommand::componentEdit(scene.object->getUUID(), before, after));

  const auto undoOutcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(undoOutcome.ok());
  replication::applyComponentEdit(*scene.objectManager, *undoOutcome.messagePayload);
  ASSERT_TRUE(history.canRedo());

  const auto before2 = transform->serialize();
  transform->setPosition({ 3, 0, 0 });
  const auto after2 = transform->serialize();
  history.record(edits::EditCommand::componentEdit(scene.object->getUUID(), before2, after2));

  EXPECT_FALSE(history.canRedo());
  EXPECT_TRUE(history.canUndo());
}

TEST(EditHistory, ClearEmptiesBothStacks)
{
  auto scene = makeScene();
  const auto transform = transformOf(scene.object);

  transform->setPosition({ 1, 0, 0 });
  const auto before = transform->serialize();
  transform->setPosition({ 2, 0, 0 });
  const auto after = transform->serialize();

  edits::EditHistory history;
  history.record(edits::EditCommand::componentEdit(scene.object->getUUID(), before, after));

  const auto undoOutcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_TRUE(history.canRedo());

  history.clear();

  EXPECT_FALSE(history.canUndo());
  EXPECT_FALSE(history.canRedo());
}

TEST(EditHistory, SequentialUndoWalksBackThroughEachIntermediateState)
{
  auto scene = makeScene();
  const auto transform = transformOf(scene.object);

  transform->setPosition({ 1, 0, 0 });
  const auto before1 = transform->serialize();
  transform->setPosition({ 2, 0, 0 });
  const auto after1 = transform->serialize();

  edits::EditHistory history;
  history.record(edits::EditCommand::componentEdit(scene.object->getUUID(), before1, after1));

  const auto before2 = transform->serialize(); // {2,0,0} - chains from the first command's after
  transform->setPosition({ 3, 0, 0 });
  const auto after2 = transform->serialize();
  history.record(edits::EditCommand::componentEdit(scene.object->getUUID(), before2, after2));

  const auto before3 = transform->serialize(); // {3,0,0}
  transform->setPosition({ 4, 0, 0 });
  const auto after3 = transform->serialize();
  history.record(edits::EditCommand::componentEdit(scene.object->getUUID(), before3, after3));

  expectNear(transform->getLocalPosition(), { 4, 0, 0 });

  auto outcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(outcome.ok());
  replication::applyComponentEdit(*scene.objectManager, *outcome.messagePayload);
  expectNear(transform->getLocalPosition(), { 3, 0, 0 });

  outcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(outcome.ok());
  replication::applyComponentEdit(*scene.objectManager, *outcome.messagePayload);
  expectNear(transform->getLocalPosition(), { 2, 0, 0 });

  outcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(outcome.ok());
  replication::applyComponentEdit(*scene.objectManager, *outcome.messagePayload);
  expectNear(transform->getLocalPosition(), { 1, 0, 0 });

  EXPECT_FALSE(history.canUndo());
}

TEST(EditHistory, DepthCapDropsTheOldestEntryNotTheNewest)
{
  auto scene = makeScene();
  const auto transform = transformOf(scene.object);

  edits::EditHistory history;

  for (std::size_t i = 0; i < edits::EditHistory::maxDepth + 5; ++i)
  {
    const auto before = transform->serialize();
    transform->setPosition({ static_cast<float>(i + 1), 0, 0 });
    const auto after = transform->serialize();
    history.record(edits::EditCommand::componentEdit(scene.object->getUUID(), before, after));
  }

  std::size_t undone = 0;
  while (history.canUndo())
  {
    const auto outcome = history.undo(*scene.objectManager);
    ASSERT_TRUE(outcome.ok());
    replication::applyComponentEdit(*scene.objectManager, *outcome.messagePayload);
    ++undone;
  }

  // Only maxDepth entries ever survive recording; the oldest 5 were evicted as new ones came in, so
  // undoing everything that is left only walks back to position 5 (the 6th edit's "before"), not to 0 -
  // proof the eviction dropped the oldest entry, not the most recently recorded one.
  EXPECT_EQ(undone, edits::EditHistory::maxDepth);
  expectNear(transform->getLocalPosition(), { 5, 0, 0 });
}

// --- Refusals: validation catches a live divergence before anything is sent, drops the failing entry and
// everything older still on the stack being popped, and leaves the other stack untouched. Each pairs a
// clean control (must succeed) against the interference case (must be refused).

TEST(EditHistory, UndoRefusesWhenTheTargetChangedUnderneathAndDropsOlderEntries)
{
  // Positive control: without interference, two chained edits undo cleanly and in order.
  {
    auto scene = makeScene();
    const auto transform = transformOf(scene.object);

    ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
                replication::buildRenameObject(scene.object->getUUID(), "Renamed")),
              replication::SceneEditResult::applied);

    edits::EditHistory history;
    history.record(edits::EditCommand::renameObject(scene.object->getUUID(), "Object", "Renamed"));

    transform->setPosition({ 1, 0, 0 });
    const auto before = transform->serialize();
    transform->setPosition({ 2, 0, 0 });
    const auto after = transform->serialize();
    history.record(edits::EditCommand::componentEdit(scene.object->getUUID(), before, after));

    auto outcome = history.undo(*scene.objectManager);
    EXPECT_EQ(outcome.result, edits::HistoryResult::applied);
    replication::applyComponentEdit(*scene.objectManager, *outcome.messagePayload);
    EXPECT_TRUE(history.canUndo());

    outcome = history.undo(*scene.objectManager);
    EXPECT_EQ(outcome.result, edits::HistoryResult::applied);
    EXPECT_FALSE(history.canUndo());
  }

  // Same setup, but something else changes the transform after it was recorded: the undo is refused, it
  // names the object that conflicted, and both the failing entry and the older rename beneath it are gone.
  {
    auto scene = makeScene();
    const auto transform = transformOf(scene.object);

    ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
                replication::buildRenameObject(scene.object->getUUID(), "Renamed")),
              replication::SceneEditResult::applied);

    edits::EditHistory history;
    history.record(edits::EditCommand::renameObject(scene.object->getUUID(), "Object", "Renamed"));

    transform->setPosition({ 1, 0, 0 });
    const auto before = transform->serialize();
    transform->setPosition({ 2, 0, 0 });
    const auto after = transform->serialize();
    history.record(edits::EditCommand::componentEdit(scene.object->getUUID(), before, after));

    transform->setPosition({ 99, 99, 99 });

    const auto outcome = history.undo(*scene.objectManager);
    EXPECT_EQ(outcome.result, edits::HistoryResult::targetChanged);
    ASSERT_TRUE(outcome.conflict.has_value());
    EXPECT_EQ(*outcome.conflict, scene.object->getUUID());
    EXPECT_FALSE(outcome.jsonPayload.has_value());
    EXPECT_FALSE(outcome.messagePayload.has_value());

    // Both entries are gone: the failing component edit and the older rename beneath it.
    EXPECT_FALSE(history.canUndo());
  }
}

TEST(EditHistory, RedoRefusesWhenTheBeforeStateChangedUnderneathAndDropsTheRestOfTheRedoStack)
{
  // Positive control: redoing both undone commands in order, without interference, succeeds.
  {
    auto scene = makeScene();
    edits::EditHistory history;
    ASSERT_NO_FATAL_FAILURE(recordTwoEditsAndUndoBoth(scene, history));

    auto outcome = history.redo(*scene.objectManager);
    EXPECT_EQ(outcome.result, edits::HistoryResult::applied);
    replication::applyComponentEdit(*scene.objectManager, *outcome.messagePayload);
    outcome = history.redo(*scene.objectManager);
    EXPECT_EQ(outcome.result, edits::HistoryResult::applied);
    EXPECT_FALSE(history.canRedo());
  }

  // Same setup, but something changes the transform while both commands are sitting on the redo stack:
  // the first redo (the older command, next in line) is refused, and the newer one behind it is dropped
  // too rather than being redone out of order.
  {
    auto scene = makeScene();
    edits::EditHistory history;
    ASSERT_NO_FATAL_FAILURE(recordTwoEditsAndUndoBoth(scene, history));

    const auto transform = transformOf(scene.object);

    ASSERT_TRUE(history.canRedo());
    expectNear(transform->getLocalPosition(), { 1, 0, 0 });

    transform->setPosition({ 999, 0, 0 });

    const auto redoOutcome = history.redo(*scene.objectManager);
    EXPECT_EQ(redoOutcome.result, edits::HistoryResult::targetChanged);
    ASSERT_TRUE(redoOutcome.conflict.has_value());
    EXPECT_EQ(*redoOutcome.conflict, scene.object->getUUID());
    EXPECT_FALSE(history.canRedo());
  }
}

TEST(EditHistory, UndoRefusesWhenTheObjectNoLongerExists)
{
  auto scene = makeScene();
  const auto transform = transformOf(scene.object);

  transform->setPosition({ 1, 0, 0 });
  const auto before = transform->serialize();
  transform->setPosition({ 2, 0, 0 });
  const auto after = transform->serialize();

  edits::EditHistory history;
  history.record(edits::EditCommand::componentEdit(scene.object->getUUID(), before, after));

  const auto objectUUID = scene.object->getUUID();
  scene.objectManager->removeObject(scene.object);
  scene.objectManager->deleteObjectsMarkedForDeletion();
  ASSERT_EQ(scene.objectManager->getObjectByUUID(objectUUID), nullptr);

  const auto outcome = history.undo(*scene.objectManager);
  EXPECT_EQ(outcome.result, edits::HistoryResult::targetMissing);
  ASSERT_TRUE(outcome.conflict.has_value());
  EXPECT_EQ(*outcome.conflict, objectUUID);
  EXPECT_FALSE(history.canUndo());

  // Positive control: the same command kind, targeting an object that IS still there, undoes fine.
  const auto other = addObject(scene, "Other");
  const auto otherTransform = transformOf(other);
  otherTransform->setPosition({ 5, 0, 0 });
  const auto otherBefore = otherTransform->serialize();
  otherTransform->setPosition({ 6, 0, 0 });
  const auto otherAfter = otherTransform->serialize();
  history.record(edits::EditCommand::componentEdit(other->getUUID(), otherBefore, otherAfter));

  const auto controlOutcome = history.undo(*scene.objectManager);
  EXPECT_EQ(controlOutcome.result, edits::HistoryResult::applied);
}

TEST(EditHistory, NotReversibleKindsRefuseCleanlyInsteadOfProducingAWrongReverse)
{
  auto scene = makeScene();

  edits::EditHistory history;

  history.record(edits::EditCommand::removeObject(scene.object->getUUID(), std::nullopt, 0,
                                                   scene.object->serialize()));
  auto outcome = history.undo(*scene.objectManager);
  EXPECT_EQ(outcome.result, edits::HistoryResult::notUndoable);
  EXPECT_FALSE(outcome.jsonPayload.has_value());
  EXPECT_FALSE(outcome.messagePayload.has_value());
  EXPECT_FALSE(history.canUndo());

  history.record(edits::EditCommand::removeComponent(scene.object->getUUID(),
                                                      transformOf(scene.object)->serialize()));
  outcome = history.undo(*scene.objectManager);
  EXPECT_EQ(outcome.result, edits::HistoryResult::notUndoable);
  EXPECT_FALSE(history.canUndo());

  history.record(edits::EditCommand::duplicateObject(scene.object->getUUID(), someOtherUUID(), std::nullopt, 1));
  outcome = history.undo(*scene.objectManager);
  EXPECT_EQ(outcome.result, edits::HistoryResult::notUndoable);
  EXPECT_FALSE(history.canUndo());

  history.record(edits::EditCommand::instantiatePrefab(someOtherUUID(), anotherUUID(), std::nullopt, 0));
  outcome = history.undo(*scene.objectManager);
  EXPECT_EQ(outcome.result, edits::HistoryResult::notUndoable);
  EXPECT_FALSE(history.canUndo());

  // Positive control: an ordinary reversible command recorded in the same history still undoes fine.
  const auto transform = transformOf(scene.object);
  transform->setPosition({ 1, 0, 0 });
  const auto before = transform->serialize();
  transform->setPosition({ 2, 0, 0 });
  const auto after = transform->serialize();
  history.record(edits::EditCommand::componentEdit(scene.object->getUUID(), before, after));

  outcome = history.undo(*scene.objectManager);
  EXPECT_EQ(outcome.result, edits::HistoryResult::applied);
}

// --- nextUndoKind()/nextRedoKind(): a caller (EditorApp::undo()/redo()) that only knows how to send the
// reverse of some kinds peeks the top of the relevant stack before calling undo()/redo(), so it can leave
// a kind it does not handle sitting there instead of letting undo()/redo() treat it as a conflict and
// drop it.

TEST(EditHistory, NextUndoAndRedoKindAreNulloptOnEmptyStacks)
{
  edits::EditHistory history;

  EXPECT_FALSE(history.nextUndoKind().has_value());
  EXPECT_FALSE(history.nextRedoKind().has_value());
}

TEST(EditHistory, NextUndoKindNamesTheTopOfTheUndoStackWithoutPoppingIt)
{
  auto scene = makeScene();
  const auto transform = transformOf(scene.object);

  transform->setPosition({ 1, 2, 3 });
  const auto before = transform->serialize();
  transform->setPosition({ 4, 5, 6 });
  const auto after = transform->serialize();

  edits::EditHistory history;
  history.record(edits::EditCommand::componentEdit(scene.object->getUUID(), before, after));
  history.record(edits::EditCommand::removeObject(scene.object->getUUID(), std::nullopt, 0,
                                                   scene.object->serialize()));

  // The most recently recorded command is on top, regardless of kind.
  ASSERT_TRUE(history.nextUndoKind().has_value());
  EXPECT_EQ(*history.nextUndoKind(), edits::CommandKind::removeObject);

  // Peeking is read-only: the stack still holds both entries, and undoing still walks them in order.
  EXPECT_TRUE(history.canUndo());
  const auto outcome = history.undo(*scene.objectManager);
  EXPECT_EQ(outcome.result, edits::HistoryResult::notUndoable);

  // The refused removeObject and everything older (the componentEdit beneath it) were dropped together,
  // the same sequential-refusal rule as any other undo() conflict.
  EXPECT_FALSE(history.canUndo());
}

TEST(EditHistory, NextRedoKindNamesTheTopOfTheRedoStackWithoutPoppingIt)
{
  auto scene = makeScene();
  const auto transform = transformOf(scene.object);

  transform->setPosition({ 1, 2, 3 });
  const auto before = transform->serialize();
  transform->setPosition({ 4, 5, 6 });
  const auto after = transform->serialize();

  edits::EditHistory history;
  history.record(edits::EditCommand::componentEdit(scene.object->getUUID(), before, after));

  const auto undoOutcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_TRUE(undoOutcome.messagePayload.has_value());
  // redo()'s validation compares the command's "before" state against the live scene, so - same as the
  // round trip tests in EditHistoryRoundTripTest.cpp - the undo has to actually be applied, not just
  // accepted, or the transform is still sitting at the "after" position and redo() correctly refuses it as changed underneath.
  ASSERT_EQ(replication::applyComponentEdit(*scene.objectManager, *undoOutcome.messagePayload),
            replication::ComponentEditResult::applied);

  ASSERT_TRUE(history.nextRedoKind().has_value());
  EXPECT_EQ(*history.nextRedoKind(), edits::CommandKind::componentEdit);

  // Still there for redo() to actually use after being peeked.
  ASSERT_TRUE(history.canRedo());
  const auto redoOutcome = history.redo(*scene.objectManager);
  EXPECT_TRUE(redoOutcome.ok());
}

// --- nextUndoIsReversible()/nextRedoIsReversible(): EditorApp::undo()/redo() gate on these instead of
// comparing nextUndoKind()/nextRedoKind() against a single allowed kind, so every reversible kind (not
// just componentEdit) gets sent, while the four non-reversible kinds stay refused and left on the stack.

TEST(EditHistory, NextUndoAndRedoIsReversibleAreFalseOnEmptyStacks)
{
  edits::EditHistory history;

  EXPECT_FALSE(history.nextUndoIsReversible());
  EXPECT_FALSE(history.nextRedoIsReversible());
}

TEST(EditHistory, NextUndoIsReversibleIsTrueForAReversibleCommandOnTopWithoutPoppingIt)
{
  auto scene = makeScene();

  edits::EditHistory history;
  history.record(edits::EditCommand::renameObject(scene.object->getUUID(), "Object", "Renamed"));

  EXPECT_TRUE(history.nextUndoIsReversible());

  // Peeking is read-only: the entry is still there to actually undo.
  ASSERT_TRUE(history.canUndo());
  const auto outcome = history.undo(*scene.objectManager);
  EXPECT_TRUE(outcome.ok());
}

TEST(EditHistory, NextUndoIsReversibleIsFalseForANonReversibleCommandOnTopAndLeavesItInPlace)
{
  auto scene = makeScene();

  edits::EditHistory history;
  history.record(edits::EditCommand::removeObject(scene.object->getUUID(), std::nullopt, 0,
                                                   scene.object->serialize()));

  EXPECT_FALSE(history.nextUndoIsReversible());

  // Positive control: the stack is untouched by the peek - the same removeObject is still on top,
  // reported the same way nextUndoKind() would name it.
  ASSERT_TRUE(history.canUndo());
  ASSERT_TRUE(history.nextUndoKind().has_value());
  EXPECT_EQ(*history.nextUndoKind(), edits::CommandKind::removeObject);
}

TEST(EditHistory, NextRedoIsReversibleIsTrueForAReversibleCommandOnTopWithoutPoppingIt)
{
  auto scene = makeScene();
  const auto transform = transformOf(scene.object);

  transform->setPosition({ 1, 2, 3 });
  const auto before = transform->serialize();
  transform->setPosition({ 4, 5, 6 });
  const auto after = transform->serialize();

  edits::EditHistory history;
  history.record(edits::EditCommand::componentEdit(scene.object->getUUID(), before, after));

  const auto undoOutcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_TRUE(undoOutcome.messagePayload.has_value());
  ASSERT_EQ(replication::applyComponentEdit(*scene.objectManager, *undoOutcome.messagePayload),
            replication::ComponentEditResult::applied);

  EXPECT_TRUE(history.nextRedoIsReversible());

  // Still there for redo() to actually use after being peeked.
  ASSERT_TRUE(history.canRedo());
  const auto redoOutcome = history.redo(*scene.objectManager);
  EXPECT_TRUE(redoOutcome.ok());
}

TEST(EditHistory, NextRedoIsReversibleIsFalseOnAnEmptyRedoStackEvenWithAnUndoStackPresent)
{
  auto scene = makeScene();

  edits::EditHistory history;
  history.record(edits::EditCommand::removeObject(scene.object->getUUID(), std::nullopt, 0,
                                                   scene.object->serialize()));

  // Positive control: the undo side reports a real (non-reversible) entry, while the redo side is simply
  // empty - two different reasons the peek can come back false.
  ASSERT_TRUE(history.canUndo());
  EXPECT_FALSE(history.canRedo());
  EXPECT_FALSE(history.nextRedoIsReversible());
}
