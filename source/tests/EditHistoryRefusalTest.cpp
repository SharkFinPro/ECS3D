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
#include <uuid.h>

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

// removeObject is the one structural kind with no faithful reverse: deleteObjectsMarkedForDeletion
// promotes the removed object's children up to its own parent rather than deleting them, and no existing
// sceneEdit op can both recreate the removed object and reclaim those already-live children in one atomic
// edit (see EditCommand.h's isReversible). removeComponent/duplicateObject/instantiatePrefab are covered
// as ordinary reversible kinds in EditHistoryRoundTripTest.cpp instead.
TEST(EditHistory, RemoveObjectRefusesCleanlyInsteadOfProducingAWrongReverse)
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

TEST(EditHistory, UndoOfARemoveComponentRefusesWhenSomethingAlreadyReaddedTheComponent)
{
  // Positive control: without interference, undo puts the removed component back.
  {
    auto scene = makeScene();

    ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
                replication::buildAddComponent(scene.object->getUUID(), "RigidBody")),
              replication::SceneEditResult::applied);
    const auto rigidBody = scene.object->getComponents().at(ComponentType::rigidBody);
    const auto removedJSON = rigidBody->serialize();

    ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
                replication::buildRemoveComponent(scene.object->getUUID(), rigidBody)),
              replication::SceneEditResult::applied);

    edits::EditHistory history;
    history.record(edits::EditCommand::removeComponent(scene.object->getUUID(), removedJSON));

    const auto outcome = history.undo(*scene.objectManager);
    EXPECT_EQ(outcome.result, edits::HistoryResult::applied);
  }

  // Same setup, but something else adds a RigidBody back onto the object before undo runs: the slot this
  // entry expects to be empty is not, so undo refuses rather than adding a second one.
  {
    auto scene = makeScene();

    ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
                replication::buildAddComponent(scene.object->getUUID(), "RigidBody")),
              replication::SceneEditResult::applied);
    const auto rigidBody = scene.object->getComponents().at(ComponentType::rigidBody);
    const auto removedJSON = rigidBody->serialize();

    ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
                replication::buildRemoveComponent(scene.object->getUUID(), rigidBody)),
              replication::SceneEditResult::applied);

    edits::EditHistory history;
    history.record(edits::EditCommand::removeComponent(scene.object->getUUID(), removedJSON));

    ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
                replication::buildAddComponent(scene.object->getUUID(), "RigidBody")),
              replication::SceneEditResult::applied);

    const auto outcome = history.undo(*scene.objectManager);
    EXPECT_EQ(outcome.result, edits::HistoryResult::targetChanged);
    ASSERT_TRUE(outcome.conflict.has_value());
    EXPECT_EQ(*outcome.conflict, scene.object->getUUID());
    EXPECT_FALSE(history.canUndo());

    // The interference is left exactly as it was - the refused undo touched nothing.
    EXPECT_TRUE(scene.object->getComponents().contains(ComponentType::rigidBody));
  }
}

TEST(EditHistory, UndoOfADuplicateObjectRefusesWhenTheDuplicateWasAlreadyRemoved)
{
  // Positive control: without interference, undo removes the whole duplicated subtree.
  {
    auto scene = makeScene();
    const auto duplicateUUID = someOtherUUID();

    ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
                replication::buildDuplicateObject(scene.object->getUUID(), &duplicateUUID)),
              replication::SceneEditResult::applied);

    edits::EditHistory history;
    history.record(
      edits::EditCommand::duplicateObject(scene.object->getUUID(), duplicateUUID, std::nullopt, 1));

    const auto outcome = history.undo(*scene.objectManager);
    EXPECT_EQ(outcome.result, edits::HistoryResult::applied);
  }

  // Same setup, but the duplicate is already gone (undone some other way, or removed directly) before
  // undo runs: nothing to reclaim, so it refuses by name rather than silently doing nothing.
  {
    auto scene = makeScene();
    const auto duplicateUUID = someOtherUUID();

    ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
                replication::buildDuplicateObject(scene.object->getUUID(), &duplicateUUID)),
              replication::SceneEditResult::applied);

    edits::EditHistory history;
    history.record(
      edits::EditCommand::duplicateObject(scene.object->getUUID(), duplicateUUID, std::nullopt, 1));

    ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
                replication::buildRemoveSubtree(duplicateUUID)),
              replication::SceneEditResult::applied);
    ASSERT_FALSE(scene.objectManager->getObjectByUUID(duplicateUUID));

    const auto outcome = history.undo(*scene.objectManager);
    EXPECT_EQ(outcome.result, edits::HistoryResult::targetMissing);
    ASSERT_TRUE(outcome.conflict.has_value());
    EXPECT_EQ(*outcome.conflict, duplicateUUID);
    EXPECT_FALSE(history.canUndo());
  }
}

namespace {
  // A minimal but well-formed prefab body (see Object::loadFromJSON) - its content does not matter to
  // these tests beyond being distinguishable from a differently-named one, for the "re-saved body" test.
  std::string minimalPrefabBody(const std::string& name = "Prefab")
  {
    return nlohmann::json{ { "uuid", uuids::to_string(anotherUUID()) },
                           { "name", name },
                           { "components", nlohmann::json::array() },
                           { "scripts", nlohmann::json::array() },
                           { "children", nlohmann::json::array() } }.dump();
  }

  // Shared setup for the instantiatePrefab redo tests below: registers a one-node prefab, instantiates
  // it, records the command, and undoes it once - so a redo is sitting ready and the instance is gone,
  // the state the positive-control and interference tests all build on.
  void setupUndoneInstantiatePrefab(AssetScene& scene, edits::EditHistory& history,
                                    uuids::uuid& prefabUUID, uuids::uuid& instanceUUID)
  {
    prefabUUID = someOtherUUID();
    const auto body = minimalPrefabBody();
    scene.assetRegistry.registerAsset({ .uuid = prefabUUID, .type = AssetType::Prefab,
                                        .path = "TestPrefab", .body = body });

    instanceUUID = uuids::uuid::from_string("00000000-0000-0000-0000-000000000002").value();
    ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
                replication::buildInstantiatePrefab(prefabUUID, nullptr, &instanceUUID),
                &scene.assetRegistry),
              replication::SceneEditResult::applied);

    history.record(
      edits::EditCommand::instantiatePrefab(prefabUUID, instanceUUID, std::nullopt, 0, body));

    const auto undoOutcome = history.undo(*scene.objectManager, &scene.assetRegistry);
    ASSERT_TRUE(undoOutcome.ok());
    ASSERT_EQ(replication::applySceneEdit(*scene.objectManager, *undoOutcome.jsonPayload),
              replication::SceneEditResult::applied);
    ASSERT_FALSE(scene.objectManager->getObjectByUUID(instanceUUID));
  }
}

// Positive control for RedoOfAnInstantiatePrefabRefusesWhenThePrefabAssetWasRemoved below: without
// interference, redo re-instantiates the prefab.
TEST(EditHistory, RedoOfAnInstantiatePrefabSucceedsWithoutInterference)
{
  AssetScene scene;
  edits::EditHistory history;
  uuids::uuid prefabUUID;
  uuids::uuid instanceUUID;
  ASSERT_NO_FATAL_FAILURE(setupUndoneInstantiatePrefab(scene, history, prefabUUID, instanceUUID));

  const auto outcome = history.redo(*scene.objectManager, &scene.assetRegistry);
  EXPECT_EQ(outcome.result, edits::HistoryResult::applied);
}

// Same setup, but the prefab asset is removed from the registry before redo runs: nothing left to
// instantiate from, so it refuses by naming the prefab rather than instantiating a stale or empty body.
TEST(EditHistory, RedoOfAnInstantiatePrefabRefusesWhenThePrefabAssetWasRemoved)
{
  AssetScene scene;
  edits::EditHistory history;
  uuids::uuid prefabUUID;
  uuids::uuid instanceUUID;
  ASSERT_NO_FATAL_FAILURE(setupUndoneInstantiatePrefab(scene, history, prefabUUID, instanceUUID));

  scene.assetRegistry.removeAsset(prefabUUID);

  const auto outcome = history.redo(*scene.objectManager, &scene.assetRegistry);
  EXPECT_EQ(outcome.result, edits::HistoryResult::targetMissing);
  ASSERT_TRUE(outcome.conflict.has_value());
  EXPECT_EQ(*outcome.conflict, prefabUUID);
  EXPECT_FALSE(history.canRedo());
}

// Positive control: RedoOfAnInstantiatePrefabSucceedsWithoutInterference above already redoes with the
// prefab body untouched. Here the same uuid is re-saved with different content (AssetRegistry updates a
// Prefab's body in place for a path it already holds - see registerAsset) before redo runs: the recorded
// command would otherwise silently instantiate the new body instead of the one the user actually
// duplicated from, so it refuses instead.
TEST(EditHistory, RedoOfAnInstantiatePrefabRefusesWhenThePrefabBodyWasReplaced)
{
  AssetScene scene;
  edits::EditHistory history;
  uuids::uuid prefabUUID;
  uuids::uuid instanceUUID;
  ASSERT_NO_FATAL_FAILURE(setupUndoneInstantiatePrefab(scene, history, prefabUUID, instanceUUID));

  scene.assetRegistry.registerAsset({ .uuid = prefabUUID, .type = AssetType::Prefab,
                                      .path = "TestPrefab", .body = minimalPrefabBody("ReSavedPrefab") });

  const auto outcome = history.redo(*scene.objectManager, &scene.assetRegistry);
  EXPECT_EQ(outcome.result, edits::HistoryResult::targetChanged);
  ASSERT_TRUE(outcome.conflict.has_value());
  EXPECT_EQ(*outcome.conflict, prefabUUID);
  EXPECT_FALSE(history.canRedo());
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

  // validateForUndo() checks the recorded "after" state against the live scene, so the rename actually
  // has to have happened - recording the command alone (without applying it, the mistake this test made
  // before) leaves the object still named "Object" and undo() correctly, deterministically refuses with
  // targetChanged on every platform, not just some.
  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
              replication::buildRenameObject(scene.object->getUUID(), "Renamed")),
            replication::SceneEditResult::applied);

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
