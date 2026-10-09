#include <gtest/gtest.h>

#include "TestScene.h"
#include "Replication.h"
#include "edits/EditCommand.h"
#include "edits/EditHistory.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Transform.h"
#include "EditHistoryFixtures.h"

#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <uuid.h>
#include <vector>

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

// --- Refusals through every failure undo()/redo() can report, for ordinary and for grouped entries. Each
// refusal drops the entry that failed and everything older on the stack being popped, and leaves the other
// stack alone; the controls below prove those stacks would otherwise have been usable.

namespace {
  struct RenamedTrio {
    std::shared_ptr<Object> older;
    std::shared_ptr<Object> target;
    std::shared_ptr<Object> newest;
  };

  void renameAndRecord(const Scene& scene, edits::EditHistory& history, const std::shared_ptr<Object>& object,
                       const std::string& newName)
  {
    const auto before = object->getName();
    ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
                replication::buildRenameObject(object->getUUID(), newName)),
              replication::SceneEditResult::applied);
    history.record(edits::EditCommand::renameObject(object->getUUID(), before, newName));
  }

  void undoAndApply(const Scene& scene, edits::EditHistory& history)
  {
    const auto outcome = history.undo(*scene.objectManager);
    ASSERT_TRUE(outcome.ok());
    ASSERT_TRUE(outcome.jsonPayload.has_value());
    ASSERT_EQ(replication::applySceneEdit(*scene.objectManager, *outcome.jsonPayload),
              replication::SceneEditResult::applied);
  }

  void redoAndApply(const Scene& scene, edits::EditHistory& history)
  {
    const auto outcome = history.redo(*scene.objectManager);
    ASSERT_TRUE(outcome.ok());
    ASSERT_TRUE(outcome.jsonPayload.has_value());
    ASSERT_EQ(replication::applySceneEdit(*scene.objectManager, *outcome.jsonPayload),
              replication::SceneEditResult::applied);
  }

  // Three renames recorded oldest to newest, so the undo stack holds [older, target, newest].
  void recordThreeRenames(Scene& scene, edits::EditHistory& history, RenamedTrio& trio)
  {
    trio.older = scene.object;
    trio.target = addObject(scene, "Target");
    trio.newest = addObject(scene, "Newest");

    ASSERT_NO_FATAL_FAILURE(renameAndRecord(scene, history, trio.older, "OlderRenamed"));
    ASSERT_NO_FATAL_FAILURE(renameAndRecord(scene, history, trio.target, "TargetRenamed"));
    ASSERT_NO_FATAL_FAILURE(renameAndRecord(scene, history, trio.newest, "NewestRenamed"));
  }

  void removeForGood(const Scene& scene, const std::shared_ptr<Object>& object)
  {
    const auto uuid = object->getUUID();
    scene.objectManager->removeObject(object);
    scene.objectManager->deleteObjectsMarkedForDeletion();
    ASSERT_EQ(scene.objectManager->getObjectByUUID(uuid), nullptr);
  }
}

TEST(EditHistory, UndoRefusedAsTargetChangedDropsTheEntryAndOlderOnesButNotTheRedoStack)
{
  // Positive control: without interference both remaining entries undo.
  {
    auto scene = makeScene();
    edits::EditHistory history;
    RenamedTrio trio;
    ASSERT_NO_FATAL_FAILURE(recordThreeRenames(scene, history, trio));

    ASSERT_NO_FATAL_FAILURE(undoAndApply(scene, history));
    ASSERT_NO_FATAL_FAILURE(undoAndApply(scene, history));
    EXPECT_EQ(history.undoDepth(), 1u);
    EXPECT_EQ(history.redoDepth(), 2u);
  }

  auto scene = makeScene();
  edits::EditHistory history;
  RenamedTrio trio;
  ASSERT_NO_FATAL_FAILURE(recordThreeRenames(scene, history, trio));
  ASSERT_NO_FATAL_FAILURE(undoAndApply(scene, history)); // newest is now on the redo stack

  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
              replication::buildRenameObject(trio.target->getUUID(), "Interloper")),
            replication::SceneEditResult::applied);

  const auto outcome = history.undo(*scene.objectManager);
  EXPECT_EQ(outcome.result, edits::HistoryResult::targetChanged);
  ASSERT_TRUE(outcome.conflict.has_value());
  EXPECT_EQ(*outcome.conflict, trio.target->getUUID());
  EXPECT_FALSE(outcome.jsonPayload.has_value());
  EXPECT_FALSE(outcome.messagePayload.has_value());

  EXPECT_EQ(history.undoDepth(), 0u);
  EXPECT_EQ(history.redoDepth(), 1u);

  ASSERT_NO_FATAL_FAILURE(redoAndApply(scene, history));
  EXPECT_EQ(trio.newest->getName(), "NewestRenamed");
}

TEST(EditHistory, UndoRefusedAsTargetMissingDropsTheEntryAndOlderOnesButNotTheRedoStack)
{
  auto scene = makeScene();
  edits::EditHistory history;
  RenamedTrio trio;
  ASSERT_NO_FATAL_FAILURE(recordThreeRenames(scene, history, trio));
  ASSERT_NO_FATAL_FAILURE(undoAndApply(scene, history));

  const auto targetUUID = trio.target->getUUID();
  ASSERT_NO_FATAL_FAILURE(removeForGood(scene, trio.target));

  const auto outcome = history.undo(*scene.objectManager);
  EXPECT_EQ(outcome.result, edits::HistoryResult::targetMissing);
  ASSERT_TRUE(outcome.conflict.has_value());
  EXPECT_EQ(*outcome.conflict, targetUUID);
  EXPECT_FALSE(outcome.jsonPayload.has_value());
  EXPECT_FALSE(outcome.messagePayload.has_value());

  EXPECT_EQ(history.undoDepth(), 0u);
  EXPECT_EQ(history.redoDepth(), 1u);

  // Positive control: the entry that stayed on the redo stack is still redoable.
  ASSERT_NO_FATAL_FAILURE(redoAndApply(scene, history));
  EXPECT_EQ(trio.newest->getName(), "NewestRenamed");
}

TEST(EditHistory, RedoRefusedAsTargetChangedDropsTheRestOfTheRedoStackButNotTheUndoStack)
{
  // Positive control: without interference both undone entries redo.
  {
    auto scene = makeScene();
    edits::EditHistory history;
    RenamedTrio trio;
    ASSERT_NO_FATAL_FAILURE(recordThreeRenames(scene, history, trio));
    ASSERT_NO_FATAL_FAILURE(undoAndApply(scene, history));
    ASSERT_NO_FATAL_FAILURE(undoAndApply(scene, history));

    ASSERT_NO_FATAL_FAILURE(redoAndApply(scene, history));
    ASSERT_NO_FATAL_FAILURE(redoAndApply(scene, history));
    EXPECT_EQ(history.undoDepth(), 3u);
    EXPECT_EQ(history.redoDepth(), 0u);
  }

  auto scene = makeScene();
  edits::EditHistory history;
  RenamedTrio trio;
  ASSERT_NO_FATAL_FAILURE(recordThreeRenames(scene, history, trio));
  ASSERT_NO_FATAL_FAILURE(undoAndApply(scene, history)); // newest
  ASSERT_NO_FATAL_FAILURE(undoAndApply(scene, history)); // target: next in line for redo

  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
              replication::buildRenameObject(trio.target->getUUID(), "Interloper")),
            replication::SceneEditResult::applied);

  const auto outcome = history.redo(*scene.objectManager);
  EXPECT_EQ(outcome.result, edits::HistoryResult::targetChanged);
  ASSERT_TRUE(outcome.conflict.has_value());
  EXPECT_EQ(*outcome.conflict, trio.target->getUUID());
  EXPECT_FALSE(outcome.jsonPayload.has_value());

  EXPECT_EQ(history.redoDepth(), 0u);
  EXPECT_EQ(history.undoDepth(), 1u);

  ASSERT_NO_FATAL_FAILURE(undoAndApply(scene, history));
  EXPECT_EQ(trio.older->getName(), "Object");
}

TEST(EditHistory, RedoRefusedAsTargetMissingDropsTheRestOfTheRedoStackButNotTheUndoStack)
{
  auto scene = makeScene();
  edits::EditHistory history;
  RenamedTrio trio;
  ASSERT_NO_FATAL_FAILURE(recordThreeRenames(scene, history, trio));
  ASSERT_NO_FATAL_FAILURE(undoAndApply(scene, history));
  ASSERT_NO_FATAL_FAILURE(undoAndApply(scene, history));

  const auto targetUUID = trio.target->getUUID();
  ASSERT_NO_FATAL_FAILURE(removeForGood(scene, trio.target));

  const auto outcome = history.redo(*scene.objectManager);
  EXPECT_EQ(outcome.result, edits::HistoryResult::targetMissing);
  ASSERT_TRUE(outcome.conflict.has_value());
  EXPECT_EQ(*outcome.conflict, targetUUID);

  EXPECT_EQ(history.redoDepth(), 0u);
  EXPECT_EQ(history.undoDepth(), 1u);

  ASSERT_NO_FATAL_FAILURE(undoAndApply(scene, history));
  EXPECT_EQ(trio.older->getName(), "Object");
}

TEST(EditHistory, RedoOfAGroupRefusesWhenOneOfItsTargetsChangedAndDropsTheRedoStack)
{
  auto scene = makeScene();
  const auto a = addObject(scene, "A");
  const auto b = addObject(scene, "B");

  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
              replication::buildBatch({ replication::buildRenameObject(a->getUUID(), "A2"),
                                        replication::buildRenameObject(b->getUUID(), "B2") })),
            replication::SceneEditResult::applied);

  edits::EditHistory history;
  history.recordBatch({ edits::EditCommand::renameObject(a->getUUID(), "A", "A2"),
                        edits::EditCommand::renameObject(b->getUUID(), "B", "B2") });

  // Positive control: the same group redoes when nothing moved.
  {
    ASSERT_NO_FATAL_FAILURE(undoAndApply(scene, history));
    ASSERT_NO_FATAL_FAILURE(redoAndApply(scene, history));
    EXPECT_EQ(a->getName(), "A2");
    EXPECT_EQ(b->getName(), "B2");
  }

  ASSERT_NO_FATAL_FAILURE(undoAndApply(scene, history));
  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
              replication::buildRenameObject(b->getUUID(), "Interloper")),
            replication::SceneEditResult::applied);

  const auto outcome = history.redo(*scene.objectManager);
  EXPECT_EQ(outcome.result, edits::HistoryResult::targetChanged);
  ASSERT_TRUE(outcome.conflict.has_value());
  EXPECT_EQ(*outcome.conflict, b->getUUID());
  EXPECT_FALSE(outcome.jsonPayload.has_value());
  EXPECT_EQ(history.redoDepth(), 0u);
}

namespace {
  // [Older recorded alone] then a group of [reorder Mover, rename Other], with Mover last of the parent's
  // children. beforeIndex is the slot the reorder claims Mover came from - the undo op sends it back there.
  void recordOlderThenReorderGroup(Scene& scene, edits::EditHistory& history,
                                   std::shared_ptr<Object>& mover, const std::size_t beforeIndex)
  {
    const auto older = addObject(scene, "Older");
    const auto other = addObject(scene, "Other");
    const auto parent = scene.object;
    addChildObject(scene, "First", parent);
    mover = addChildObject(scene, "Mover", parent);

    ASSERT_NO_FATAL_FAILURE(renameAndRecord(scene, history, older, "OlderRenamed"));
    ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
                replication::buildRenameObject(other->getUUID(), "OtherRenamed")),
              replication::SceneEditResult::applied);

    history.recordBatch({
      edits::EditCommand::reorderObject(mover->getUUID(), parent->getUUID(), beforeIndex, parent->getUUID(), 1),
      edits::EditCommand::renameObject(other->getUUID(), "Other", "OtherRenamed")
    });
  }
}

// The group's own validation passes, but the undo op it builds for the reorder names a slot past the end of
// the list, which the scene edit then refuses on the scratch copy.
TEST(EditHistory, UndoOfAGroupRefusesWhenAnUndoOpWouldBeRejectedAndDropsOlderEntries)
{
  // Positive control: the same group with a reachable slot undoes as one batch, leaving the older entry.
  {
    auto scene = makeScene();
    edits::EditHistory history;
    std::shared_ptr<Object> mover;
    ASSERT_NO_FATAL_FAILURE(recordOlderThenReorderGroup(scene, history, mover, 0));

    const auto outcome = history.undo(*scene.objectManager);
    ASSERT_TRUE(outcome.ok());
    ASSERT_TRUE(outcome.jsonPayload.has_value());
    EXPECT_EQ(outcome.jsonPayload->at("op"), "batch");
    EXPECT_EQ(history.undoDepth(), 1u);
    EXPECT_EQ(history.redoDepth(), 1u);
  }

  auto scene = makeScene();
  edits::EditHistory history;
  std::shared_ptr<Object> mover;
  ASSERT_NO_FATAL_FAILURE(recordOlderThenReorderGroup(scene, history, mover, 7));

  const auto outcome = history.undo(*scene.objectManager);
  EXPECT_EQ(outcome.result, edits::HistoryResult::targetChanged);
  ASSERT_TRUE(outcome.conflict.has_value());
  EXPECT_EQ(*outcome.conflict, mover->getUUID());
  EXPECT_FALSE(outcome.jsonPayload.has_value());
  EXPECT_EQ(history.undoDepth(), 0u);
  EXPECT_EQ(history.redoDepth(), 0u);
}

namespace {
  // Mover is the last of six siblings and the group's reorder claims it moved there from slot 0. The group
  // is then undone and applied, so it sits on the redo stack with Mover at the front.
  void recordAndUndoReorderGroupOverSixSiblings(Scene& scene, edits::EditHistory& history,
                                                std::shared_ptr<Object>& mover,
                                                std::vector<std::shared_ptr<Object>>& others)
  {
    const auto older = addObject(scene, "Older");
    const auto other = addObject(scene, "Other");
    const auto parent = scene.object;
    for (int index = 0; index < 5; ++index)
    {
      others.push_back(addChildObject(scene, "Sibling" + std::to_string(index), parent));
    }
    mover = addChildObject(scene, "Mover", parent);

    ASSERT_NO_FATAL_FAILURE(renameAndRecord(scene, history, older, "OlderRenamed"));
    ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
                replication::buildRenameObject(other->getUUID(), "OtherRenamed")),
              replication::SceneEditResult::applied);

    history.recordBatch({
      edits::EditCommand::reorderObject(mover->getUUID(), parent->getUUID(), 0, parent->getUUID(), 5),
      edits::EditCommand::renameObject(other->getUUID(), "Other", "OtherRenamed")
    });

    ASSERT_NO_FATAL_FAILURE(undoAndApply(scene, history));
    ASSERT_EQ(parent->getChildren().front(), mover);
  }
}

// Redo's validation passes (Mover is at the slot the command recorded), but the siblings it was meant to
// land behind are gone, so the redo op names a slot past the end and the scratch copy refuses it.
TEST(EditHistory, RedoOfAGroupRefusesWhenARedoOpWouldBeRejectedAndLeavesTheUndoStack)
{
  // Positive control: with the siblings still there, the same redo is applied as one batch.
  {
    auto scene = makeScene();
    edits::EditHistory history;
    std::shared_ptr<Object> mover;
    std::vector<std::shared_ptr<Object>> others;
    ASSERT_NO_FATAL_FAILURE(recordAndUndoReorderGroupOverSixSiblings(scene, history, mover, others));

    const auto outcome = history.redo(*scene.objectManager);
    ASSERT_TRUE(outcome.ok());
    ASSERT_TRUE(outcome.jsonPayload.has_value());
    EXPECT_EQ(outcome.jsonPayload->at("op"), "batch");
    EXPECT_EQ(history.undoDepth(), 2u);
  }

  auto scene = makeScene();
  edits::EditHistory history;
  std::shared_ptr<Object> mover;
  std::vector<std::shared_ptr<Object>> others;
  ASSERT_NO_FATAL_FAILURE(recordAndUndoReorderGroupOverSixSiblings(scene, history, mover, others));

  for (const auto& sibling : others)
  {
    ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
                replication::buildRemoveSubtree(sibling->getUUID())),
              replication::SceneEditResult::applied);
  }
  ASSERT_EQ(scene.object->getChildren().size(), 1u);

  const auto outcome = history.redo(*scene.objectManager);
  EXPECT_EQ(outcome.result, edits::HistoryResult::targetChanged);
  ASSERT_TRUE(outcome.conflict.has_value());
  EXPECT_EQ(*outcome.conflict, mover->getUUID());
  EXPECT_FALSE(outcome.jsonPayload.has_value());
  EXPECT_EQ(history.redoDepth(), 0u);
  EXPECT_EQ(history.undoDepth(), 1u); // the older entry, untouched
}

// --- reportUndoRejected / reportRedoRejected: the entry undo()/redo() just moved to the opposite stack
// is the one dropped.

TEST(EditHistory, ReportUndoRejectedDropsOnlyTheEntryUndoJustMovedToTheRedoStack)
{
  auto scene = makeScene();
  edits::EditHistory history;
  RenamedTrio trio;
  ASSERT_NO_FATAL_FAILURE(recordThreeRenames(scene, history, trio));

  ASSERT_NO_FATAL_FAILURE(undoAndApply(scene, history)); // newest
  ASSERT_NO_FATAL_FAILURE(undoAndApply(scene, history)); // target, now the top of the redo stack
  ASSERT_EQ(history.redoDepth(), 2u);
  ASSERT_EQ(history.nextRedoLabel(*scene.objectManager), std::optional<std::string>("Rename TargetRenamed"));

  history.reportUndoRejected();

  EXPECT_EQ(history.redoDepth(), 1u);
  EXPECT_EQ(history.nextRedoLabel(*scene.objectManager), std::optional<std::string>("Rename NewestRenamed"));
  EXPECT_EQ(history.undoDepth(), 1u);
}

TEST(EditHistory, ReportRedoRejectedDropsOnlyTheEntryRedoJustMovedBackToTheUndoStack)
{
  auto scene = makeScene();
  edits::EditHistory history;
  RenamedTrio trio;
  ASSERT_NO_FATAL_FAILURE(recordThreeRenames(scene, history, trio));

  ASSERT_NO_FATAL_FAILURE(undoAndApply(scene, history));
  ASSERT_NO_FATAL_FAILURE(redoAndApply(scene, history)); // newest is back on top of the undo stack
  ASSERT_EQ(history.undoDepth(), 3u);
  ASSERT_EQ(history.nextUndoLabel(*scene.objectManager), std::optional<std::string>("Rename NewestRenamed"));

  history.reportRedoRejected();

  EXPECT_EQ(history.undoDepth(), 2u);
  EXPECT_EQ(history.nextUndoLabel(*scene.objectManager), std::optional<std::string>("Rename TargetRenamed"));
  EXPECT_EQ(history.redoDepth(), 0u);
}

TEST(EditHistory, ReportingARejectionWithNothingOnTheOppositeStackChangesNothing)
{
  auto scene = makeScene();
  edits::EditHistory history;

  history.reportUndoRejected();
  history.reportRedoRejected();
  EXPECT_FALSE(history.canUndo());
  EXPECT_FALSE(history.canRedo());

  // Positive control: with a recorded entry on the undo stack only, reporting an undo rejection leaves it
  // alone (the redo stack it would pop is empty), while reporting a redo rejection drops it.
  ASSERT_NO_FATAL_FAILURE(renameAndRecord(scene, history, scene.object, "Renamed"));

  history.reportUndoRejected();
  EXPECT_EQ(history.undoDepth(), 1u);

  history.reportRedoRejected();
  EXPECT_EQ(history.undoDepth(), 0u);
}
