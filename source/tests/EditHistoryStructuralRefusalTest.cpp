#include <gtest/gtest.h>

#include "TestScene.h"
#include "Replication.h"
#include "edits/EditCommand.h"
#include "edits/EditHistory.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "EditHistoryFixtures.h"

#include <nlohmann/json.hpp>
#include <optional>
#include <uuid.h>
#include <array>
#include <cstddef>
#include <memory>
#include <string>

namespace {
  using namespace editHistoryFixtures;
}

namespace {
  struct RemovedXWithOneChild {
    std::shared_ptr<Object> parent;
    std::shared_ptr<Object> c1;
    uuids::uuid xUUID;
  };

  // Parent with a single child X, X with a single child C1 - removed for real (recorded, then applied),
  // the minimal shape every removeObject-undo refusal test below needs (C1 promoted to Parent).
  RemovedXWithOneChild buildAndRemoveXWithOneChild(Scene& scene, edits::EditHistory& history)
  {
    RemovedXWithOneChild removed;
    removed.parent = addObject(scene, "Parent");
    const auto x = addChildObject(scene, "X", removed.parent);
    removed.c1 = addChildObject(scene, "C1", x);

    removed.xUUID = x->getUUID();
    const auto parentUUID = removed.parent->getUUID();
    history.record(edits::EditCommand::removeObject(removed.xUUID, parentUUID, 0, x->serialize()));

    EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, replication::buildRemoveObject(removed.xUUID)),
              replication::SceneEditResult::applied);
    EXPECT_EQ(removed.c1->getParent(), removed.parent);

    return removed;
  }
}

// removeObject's undo (restoreObject's "adopt" field - see EditCommand.h's isReversible) validates
// against the live scene the same way every other kind does: it is refused, naming the conflicting uuid,
// when a recorded direct child has moved out from under the parent it was promoted to before undo runs.
// The ordinary round trip (no interference) is EditHistoryRoundTripTest.cpp's
// UndoAndRedoRoundTripARemoveObjectWithAPromotedChild - this is its refusal counterpart.
TEST(EditHistory, UndoOfARemoveObjectRefusesWhenAPromotedChildWasMovedElsewhereFirst)
{
  // Positive control: without interference, undo restores the object and reclaims its child, as the round
  // trip test already covers in detail - checked minimally here to prove the setup below is otherwise sound.
  {
    auto scene = makeScene();
    edits::EditHistory history;
    const auto removed = buildAndRemoveXWithOneChild(scene, history);

    const auto outcome = history.undo(*scene.objectManager);
    EXPECT_EQ(outcome.result, edits::HistoryResult::applied);
    ASSERT_TRUE(outcome.jsonPayload.has_value());
    EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *outcome.jsonPayload),
              replication::SceneEditResult::applied);
    EXPECT_NE(scene.objectManager->getObjectByUUID(removed.xUUID), nullptr);
    EXPECT_EQ(removed.c1->getParent(), scene.objectManager->getObjectByUUID(removed.xUUID));
  }

  // Same setup, but something else reparents the promoted child away from Parent before undo runs: the
  // slot this entry expects to reclaim C1 from is no longer where C1 lives, so undo refuses by naming C1
  // rather than reclaiming the wrong object or silently doing nothing.
  {
    auto scene = makeScene();
    edits::EditHistory history;
    const auto removed = buildAndRemoveXWithOneChild(scene, history);
    const auto elsewhere = addObject(scene, "Elsewhere");

    const auto elsewhereUUID = elsewhere->getUUID();
    ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
                replication::buildReparentObject(removed.c1->getUUID(), &elsewhereUUID)),
              replication::SceneEditResult::applied);
    ASSERT_EQ(removed.c1->getParent(), elsewhere);

    const auto outcome = history.undo(*scene.objectManager);
    EXPECT_EQ(outcome.result, edits::HistoryResult::targetChanged);
    ASSERT_TRUE(outcome.conflict.has_value());
    EXPECT_EQ(*outcome.conflict, removed.c1->getUUID());
    EXPECT_FALSE(history.canUndo());

    // The interference is left exactly as it was - the refused undo touched nothing, and X is still gone.
    EXPECT_EQ(scene.objectManager->getObjectByUUID(removed.xUUID), nullptr);
    EXPECT_EQ(removed.c1->getParent(), elsewhere);
  }
}

// Same shape, but the divergence is the removed object itself reappearing (someone else restored an
// object under the same uuid) rather than one of its children moving.
TEST(EditHistory, UndoOfARemoveObjectRefusesWhenTheObjectItselfAlreadyExists)
{
  auto scene = makeScene();

  edits::EditHistory history;
  history.record(edits::EditCommand::removeObject(scene.object->getUUID(), std::nullopt, 0,
                                                   scene.object->serialize()));

  // The object was never actually removed (only the command was recorded, the way a stale/duplicated
  // history entry could arise) - "after" a removal is absence, and it is still right there.
  const auto outcome = history.undo(*scene.objectManager);
  EXPECT_EQ(outcome.result, edits::HistoryResult::targetChanged);
  ASSERT_TRUE(outcome.conflict.has_value());
  EXPECT_EQ(*outcome.conflict, scene.object->getUUID());
  EXPECT_FALSE(history.canUndo());

  // Positive control: an ordinary reversible command recorded in the same history still undoes fine.
  const auto transform = transformOf(scene.object);
  transform->setPosition({ 1, 0, 0 });
  const auto before = transform->serialize();
  transform->setPosition({ 2, 0, 0 });
  const auto after = transform->serialize();
  history.record(edits::EditCommand::componentEdit(scene.object->getUUID(), before, after));

  const auto controlOutcome = history.undo(*scene.objectManager);
  EXPECT_EQ(controlOutcome.result, edits::HistoryResult::applied);
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
  // Recorded but never actually applied to the scene, so "after" a removal (the object being absent) does
  // not hold - undo() will refuse this one as targetChanged rather than apply it, which is exactly what
  // this test wants: something on top for nextUndoKind() to name regardless of kind.
  history.record(edits::EditCommand::removeObject(scene.object->getUUID(), std::nullopt, 0,
                                                   scene.object->serialize()));

  // The most recently recorded command is on top, regardless of kind.
  ASSERT_TRUE(history.nextUndoKind().has_value());
  EXPECT_EQ(*history.nextUndoKind(), edits::CommandKind::removeObject);
  EXPECT_EQ(history.undoDepth(), 2u);

  // Peeking is read-only: the stack still holds both entries, and undoing still walks them in order.
  EXPECT_TRUE(history.canUndo());
  const auto outcome = history.undo(*scene.objectManager);
  EXPECT_EQ(outcome.result, edits::HistoryResult::targetChanged);

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
// comparing nextUndoKind()/nextRedoKind() against a single allowed kind, so every reversible kind gets
// sent. Every CommandKind is reversible today (see EditCommand.h's isReversible), so there is no longer a
// kind that exercises the "false for a real entry" case below beyond the empty-stack ones - the mechanism
// stays as the extension point for a future kind that has none.

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
  history.record(edits::EditCommand::renameObject(scene.object->getUUID(), "Object", "Renamed"));

  // Positive control: the undo side reports a real, reversible entry, while the redo side is simply empty
  // (nothing has been undone yet) - two different reasons the peek can come back false for the redo side
  // specifically, not the same reason as an empty undo stack.
  ASSERT_TRUE(history.canUndo());
  EXPECT_TRUE(history.nextUndoIsReversible());
  EXPECT_FALSE(history.canRedo());
  EXPECT_FALSE(history.nextRedoIsReversible());
}

// --- Refusal matrix: for each kind, the control world accepts the entry and every variant breaks exactly one
// thing the validation compares. Every case sits over an older entry, so a refusal also has to drop the rest
// of the stack it popped from, and none of them touches the other stack.

namespace {
  using edits::HistoryResult;

  void eraseObject(const fixtures::Scene& scene, const std::shared_ptr<Object>& object)
  {
    scene.objectManager->removeObject(object);
    scene.objectManager->deleteObjectsMarkedForDeletion();
  }

  // The older entry every case rests on, valid in any world that still holds the anchor.
  edits::EditCommand olderRename(const std::shared_ptr<Object>& anchor)
  {
    anchor->setName("Renamed");
    return edits::EditCommand::renameObject(anchor->getUUID(), "Anchor", "Renamed");
  }

  // Undo stack [older rename, command], the command on top.
  void stageUndo(edits::EditHistory& history, const std::shared_ptr<Object>& anchor,
                 const edits::EditCommand& command)
  {
    history.record(olderRename(anchor));
    history.record(command);
  }

  void expectUndo(const fixtures::Scene& scene, const AssetRegistry* assets, edits::EditHistory& history,
                  const HistoryResult expected, const uuids::uuid& conflict)
  {
    const auto outcome = history.undo(*scene.objectManager, assets);
    EXPECT_EQ(outcome.result, expected);

    if (expected == HistoryResult::applied)
    {
      EXPECT_EQ(history.undoDepth(), 1u);
      EXPECT_EQ(history.redoDepth(), 1u);
      return;
    }

    ASSERT_TRUE(outcome.conflict.has_value());
    EXPECT_EQ(*outcome.conflict, conflict);
    EXPECT_FALSE(outcome.jsonPayload.has_value());
    EXPECT_FALSE(outcome.messagePayload.has_value());
    EXPECT_EQ(history.undoDepth(), 0u);
  }

  // Both entries undone, the command last, so the redo stack is [rename, command] with the command on top.
  // The world must satisfy the command's after state when this runs.
  void stageRedo(const fixtures::Scene& scene, const AssetRegistry* assets, edits::EditHistory& history,
                 const std::shared_ptr<Object>& anchor, const edits::EditCommand& command)
  {
    history.record(command);
    history.record(olderRename(anchor));
    ASSERT_EQ(history.undo(*scene.objectManager, assets).result, HistoryResult::applied);
    ASSERT_EQ(history.undo(*scene.objectManager, assets).result, HistoryResult::applied);
    ASSERT_EQ(history.redoDepth(), 2u);
  }

  void expectRedo(const fixtures::Scene& scene, const AssetRegistry* assets, edits::EditHistory& history,
                  const HistoryResult expected, const uuids::uuid& conflict)
  {
    const auto outcome = history.redo(*scene.objectManager, assets);
    EXPECT_EQ(outcome.result, expected);

    if (expected == HistoryResult::applied)
    {
      EXPECT_EQ(history.redoDepth(), 1u);
      EXPECT_EQ(history.undoDepth(), 1u);
      return;
    }

    ASSERT_TRUE(outcome.conflict.has_value());
    EXPECT_EQ(*outcome.conflict, conflict);
    EXPECT_FALSE(outcome.jsonPayload.has_value());
    EXPECT_FALSE(outcome.messagePayload.has_value());
    EXPECT_EQ(history.redoDepth(), 0u);
  }

  void reparent(const fixtures::Scene& scene, const std::shared_ptr<Object>& object,
                const std::shared_ptr<Object>& parent)
  {
    const auto parentUUID = parent->getUUID();
    ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
                replication::buildReparentObject(object->getUUID(), &parentUUID)),
              replication::SceneEditResult::applied);
  }

  // Control, then the object moved to a third parent, then the object gone.
  constexpr std::array<HistoryResult, 3> controlMovedGone{
    HistoryResult::applied, HistoryResult::targetChanged, HistoryResult::targetMissing };
}

TEST(EditHistoryRefusalMatrix, UndoOfAReparentNeedsTheObjectUnderTheRecordedAfterParent)
{
  for (std::size_t variant = 0; variant < controlMovedGone.size(); ++variant)
  {
    SCOPED_TRACE(variant);
    auto scene = makeScene();
    const auto p1 = addObject(scene, "P1");
    const auto p3 = addObject(scene, "P3");
    const auto object = addChildObject(scene, "Obj", variant == 1 ? p3 : p1);
    const auto objectUUID = object->getUUID();
    if (variant == 2)
    {
      eraseObject(scene, object);
    }

    edits::EditHistory history;
    stageUndo(history, scene.object, edits::EditCommand::reparentObject(objectUUID, std::nullopt, p1->getUUID()));
    expectUndo(scene, nullptr, history, controlMovedGone[variant], objectUUID);
  }
}

TEST(EditHistoryRefusalMatrix, RedoOfAReparentNeedsTheObjectUnderTheRecordedBeforeParent)
{
  for (std::size_t variant = 0; variant < controlMovedGone.size(); ++variant)
  {
    SCOPED_TRACE(variant);
    auto scene = makeScene();
    const auto p1 = addObject(scene, "P1");
    const auto p2 = addObject(scene, "P2");
    const auto p3 = addObject(scene, "P3");
    const auto object = addChildObject(scene, "Obj", p1);
    const auto objectUUID = object->getUUID();

    edits::EditHistory history;
    ASSERT_NO_FATAL_FAILURE(stageRedo(scene, nullptr, history, scene.object,
      edits::EditCommand::reparentObject(objectUUID, p2->getUUID(), p1->getUUID())));

    if (variant == 2)
    {
      eraseObject(scene, object);
    }
    else
    {
      ASSERT_NO_FATAL_FAILURE(reparent(scene, object, variant == 1 ? p3 : p2));
    }
    expectRedo(scene, nullptr, history, controlMovedGone[variant], objectUUID);
  }
}

TEST(EditHistoryRefusalMatrix, UndoOfAReorderNeedsTheRecordedAfterParentEvenAtTheSameIndex)
{
  for (std::size_t variant = 0; variant < 2; ++variant)
  {
    SCOPED_TRACE(variant);
    auto scene = makeScene();
    const auto p1 = addObject(scene, "P1");
    const auto p2 = addObject(scene, "P2");
    const auto p3 = addObject(scene, "P3");
    const auto object = addChildObject(scene, "Obj", variant == 1 ? p3 : p2);

    edits::EditHistory history;
    stageUndo(history, scene.object,
              edits::EditCommand::reorderObject(object->getUUID(), p1->getUUID(), 0, p2->getUUID(), 0));
    expectUndo(scene, nullptr, history, controlMovedGone[variant], object->getUUID());
  }
}

TEST(EditHistoryRefusalMatrix, RedoOfAReorderNeedsTheRecordedBeforeParentEvenAtTheSameIndex)
{
  for (std::size_t variant = 0; variant < 2; ++variant)
  {
    SCOPED_TRACE(variant);
    auto scene = makeScene();
    const auto p1 = addObject(scene, "P1");
    const auto p2 = addObject(scene, "P2");
    const auto p3 = addObject(scene, "P3");
    const auto object = addChildObject(scene, "Obj", p2);

    edits::EditHistory history;
    ASSERT_NO_FATAL_FAILURE(stageRedo(scene, nullptr, history, scene.object,
      edits::EditCommand::reorderObject(object->getUUID(), p1->getUUID(), 0, p2->getUUID(), 0)));

    ASSERT_NO_FATAL_FAILURE(reparent(scene, object, variant == 1 ? p3 : p1));
    expectRedo(scene, nullptr, history, controlMovedGone[variant], object->getUUID());
  }
}

// Undo goes back to a non-null parent and redo goes forward to the root, the two directions the
// root-to-parent round trip never takes.
TEST(EditHistoryRefusalMatrix, ReparentRoundTripsFromAParentToTheRootAndBack)
{
  auto scene = makeScene();
  const auto parent = addObject(scene, "Parent");
  const auto object = addChildObject(scene, "Obj", parent);

  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager, replication::buildReparentObject(object->getUUID())),
            replication::SceneEditResult::applied);
  ASSERT_EQ(object->getParent(), nullptr);

  edits::EditHistory history;
  history.record(edits::EditCommand::reparentObject(object->getUUID(), parent->getUUID(), std::nullopt));

  const auto undoOutcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager, *undoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_EQ(object->getParent(), parent);

  const auto redoOutcome = history.redo(*scene.objectManager);
  ASSERT_TRUE(redoOutcome.ok());
  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager, *redoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_EQ(object->getParent(), nullptr);
}

TEST(EditHistoryRefusalMatrix, UndoOfARemoveObjectNeedsItsRecordedParentStillThere)
{
  for (const bool parentGone : { false, true })
  {
    SCOPED_TRACE(parentGone);
    auto scene = makeScene();
    const auto parent = addObject(scene, "Parent");
    const auto x = addChildObject(scene, "X", parent);
    const auto xUUID = x->getUUID();
    const auto parentUUID = parent->getUUID();
    const auto removed = edits::EditCommand::removeObject(xUUID, parentUUID, 0, x->serialize());

    ASSERT_EQ(replication::applySceneEdit(*scene.objectManager, replication::buildRemoveObject(xUUID)),
              replication::SceneEditResult::applied);
    if (parentGone)
    {
      eraseObject(scene, parent);
    }

    edits::EditHistory history;
    stageUndo(history, scene.object, removed);
    expectUndo(scene, nullptr, history, parentGone ? HistoryResult::targetMissing : HistoryResult::applied,
               parentUUID);
  }
}

TEST(EditHistoryRefusalMatrix, UndoOfARemoveObjectNeedsEveryRecordedChildStillLiving)
{
  for (const bool childGone : { false, true })
  {
    SCOPED_TRACE(childGone);
    auto scene = makeScene();
    const auto parent = addObject(scene, "Parent");
    const auto x = addChildObject(scene, "X", parent);
    const auto child = addChildObject(scene, "C1", x);
    const auto childUUID = child->getUUID();
    const auto removed = edits::EditCommand::removeObject(x->getUUID(), parent->getUUID(), 0, x->serialize());

    ASSERT_EQ(replication::applySceneEdit(*scene.objectManager, replication::buildRemoveObject(x->getUUID())),
              replication::SceneEditResult::applied);
    if (childGone)
    {
      eraseObject(scene, child);
    }

    edits::EditHistory history;
    stageUndo(history, scene.object, removed);
    expectUndo(scene, nullptr, history, childGone ? HistoryResult::targetMissing : HistoryResult::applied,
               childUUID);
  }
}

TEST(EditHistoryRefusalMatrix, UndoOfARemoveObjectRefusesARecordedChildWithAnUnreadableUUID)
{
  for (const bool corrupt : { false, true })
  {
    SCOPED_TRACE(corrupt);
    auto scene = makeScene();
    const auto parent = addObject(scene, "Parent");
    const auto x = addChildObject(scene, "X", parent);
    addChildObject(scene, "C1", x);
    const auto xUUID = x->getUUID();

    auto subtree = x->serialize();
    ASSERT_EQ(subtree.at("children").size(), 1u);
    if (corrupt)
    {
      subtree["children"][0]["uuid"] = "not-a-uuid";
    }
    const auto removed = edits::EditCommand::removeObject(xUUID, parent->getUUID(), 0, subtree);

    ASSERT_EQ(replication::applySceneEdit(*scene.objectManager, replication::buildRemoveObject(xUUID)),
              replication::SceneEditResult::applied);

    edits::EditHistory history;
    stageUndo(history, scene.object, removed);
    expectUndo(scene, nullptr, history, corrupt ? HistoryResult::targetChanged : HistoryResult::applied, xUUID);
  }
}

TEST(EditHistoryRefusalMatrix, UndoOfAnAddObjectAndAnAddComponentNeedTheirTargetsStillThere)
{
  for (const bool gone : { false, true })
  {
    SCOPED_TRACE(gone);
    auto scene = makeScene();
    const auto added = addObject(scene, "Added");
    const auto addedUUID = added->getUUID();
    if (gone)
    {
      eraseObject(scene, added);
    }

    edits::EditHistory history;
    stageUndo(history, scene.object, edits::EditCommand::addObject(addedUUID, std::nullopt, "Added", 1));
    expectUndo(scene, nullptr, history, gone ? HistoryResult::targetMissing : HistoryResult::applied, addedUUID);
  }

  // The object is there but the component the command added is not.
  for (const bool componentGone : { false, true })
  {
    SCOPED_TRACE(componentGone);
    auto scene = makeScene();
    const auto object = addObject(scene, "Object2");
    if (!componentGone)
    {
      fixtures::addRigidBody(object);
    }

    edits::EditHistory history;
    stageUndo(history, scene.object, edits::EditCommand::addComponent(object->getUUID(), "RigidBody"));
    expectUndo(scene, nullptr, history, componentGone ? HistoryResult::targetMissing : HistoryResult::applied,
               object->getUUID());
  }
}

TEST(EditHistoryRefusalMatrix, RedoOfADuplicateNeedsItsSourceAndAFreeDuplicateUUID)
{
  enum Variant { control, sourceGone, duplicateStillThere };
  const std::array<HistoryResult, 3> expected{
    HistoryResult::applied, HistoryResult::targetMissing, HistoryResult::targetChanged };

  for (const int variant : { control, sourceGone, duplicateStillThere })
  {
    SCOPED_TRACE(variant);
    auto scene = makeScene();
    const auto source = addObject(scene, "Source");
    const auto duplicate = addObject(scene, "Duplicate");
    const auto sourceUUID = source->getUUID();
    const auto duplicateUUID = duplicate->getUUID();

    edits::EditHistory history;
    ASSERT_NO_FATAL_FAILURE(stageRedo(scene, nullptr, history, scene.object,
      edits::EditCommand::duplicateObject(sourceUUID, duplicateUUID, std::nullopt, 2)));

    if (variant != duplicateStillThere)
    {
      eraseObject(scene, duplicate);
    }
    if (variant == sourceGone)
    {
      eraseObject(scene, source);
    }
    expectRedo(scene, nullptr, history, expected[variant], variant == sourceGone ? sourceUUID : duplicateUUID);
  }
}

TEST(EditHistoryRefusalMatrix, RedoOfAnInstantiatePrefabNeedsItsParentAndAFreeInstanceUUID)
{
  enum Variant { control, parentGone, instanceStillThere };
  const std::array<HistoryResult, 3> expected{
    HistoryResult::applied, HistoryResult::targetMissing, HistoryResult::targetChanged };

  for (const int variant : { control, parentGone, instanceStillThere })
  {
    SCOPED_TRACE(variant);
    AssetScene scene;
    const auto anchor = addObject(scene, "Object");
    const auto parent = addObject(scene, "Parent");
    const auto instance = addChildObject(scene, "Instance", parent);
    const auto parentUUID = parent->getUUID();
    const auto instanceUUID = instance->getUUID();

    const std::string body = minimalPrefabBody();
    const auto prefabUUID = someOtherUUID();
    scene.assetRegistry.registerAsset({ .uuid = prefabUUID, .type = AssetType::Prefab,
                                        .path = "TestPrefab", .body = body });

    edits::EditHistory history;
    ASSERT_NO_FATAL_FAILURE(stageRedo(scene, &scene.assetRegistry, history, anchor,
      edits::EditCommand::instantiatePrefab(prefabUUID, instanceUUID, parentUUID, 0, body)));

    if (variant != instanceStillThere)
    {
      eraseObject(scene, instance);
    }
    if (variant == parentGone)
    {
      eraseObject(scene, parent);
    }
    expectRedo(scene, &scene.assetRegistry, history, expected[variant],
               variant == parentGone ? parentUUID : instanceUUID);
  }
}

TEST(EditHistoryRefusalMatrix, RedoOfAnAddObjectNeedsItsParentStillThere)
{
  for (const bool parentGone : { false, true })
  {
    SCOPED_TRACE(parentGone);
    auto scene = makeScene();
    const auto parent = addObject(scene, "Parent");
    const auto added = addChildObject(scene, "Added", parent);
    const auto parentUUID = parent->getUUID();

    edits::EditHistory history;
    ASSERT_NO_FATAL_FAILURE(stageRedo(scene, nullptr, history, scene.object,
      edits::EditCommand::addObject(added->getUUID(), parentUUID, "Added", 0)));

    eraseObject(scene, added);
    if (parentGone)
    {
      eraseObject(scene, parent);
    }
    expectRedo(scene, nullptr, history, parentGone ? HistoryResult::targetMissing : HistoryResult::applied,
               parentUUID);
  }
}

TEST(EditHistoryRefusalMatrix, RedoOfARemoveComponentNeedsTheComponentToStillHoldWhatWasRemoved)
{
  for (const bool edited : { false, true })
  {
    SCOPED_TRACE(edited);
    auto scene = makeScene();
    const auto object = addObject(scene, "Object2");

    const auto donor = addObject(scene, "Donor");
    const auto removedJSON = fixtures::addRigidBody(donor)->serialize();

    edits::EditHistory history;
    ASSERT_NO_FATAL_FAILURE(stageRedo(scene, nullptr, history, scene.object,
                                      edits::EditCommand::removeComponent(object->getUUID(), removedJSON)));

    const auto rigidBody = fixtures::addRigidBody(object);
    if (edited)
    {
      rigidBody->setMass(rigidBody->getMass() + 3.0f);
    }
    expectRedo(scene, nullptr, history, edited ? HistoryResult::targetChanged : HistoryResult::applied,
               object->getUUID());
  }
}

// --- Asset kinds, against the live registry.

namespace {
  const std::string modelPath = "models/a.obj";

  void registerModel(AssetScene& scene, const std::string& path, const std::string& className = {},
                     const std::string& displayName = {})
  {
    scene.assetRegistry.registerAsset({ .uuid = someOtherUUID(), .type = AssetType::Model, .path = path,
                                        .className = className });
    if (!displayName.empty())
    {
      scene.assetRegistry.renameAsset(someOtherUUID(), displayName);
    }
    ASSERT_NE(scene.assetRegistry.getByUUID(someOtherUUID()), nullptr);
  }
}

TEST(EditHistoryRefusalMatrix, UndoOfAnAddAssetNeedsTheAssetStillRegistered)
{
  for (const bool gone : { false, true })
  {
    SCOPED_TRACE(gone);
    AssetScene scene;
    const auto anchor = addObject(scene, "Object");
    if (!gone)
    {
      ASSERT_NO_FATAL_FAILURE(registerModel(scene, modelPath));
    }

    edits::EditHistory history;
    stageUndo(history, anchor, edits::EditCommand::addAsset(someOtherUUID(), AssetType::Model, modelPath, "", ""));
    expectUndo(scene, &scene.assetRegistry, history, gone ? HistoryResult::targetMissing : HistoryResult::applied,
               someOtherUUID());
  }
}

TEST(EditHistoryRefusalMatrix, UndoOfARemoveAssetNeedsTheAssetStillAbsent)
{
  for (const bool readded : { false, true })
  {
    SCOPED_TRACE(readded);
    AssetScene scene;
    const auto anchor = addObject(scene, "Object");
    if (readded)
    {
      ASSERT_NO_FATAL_FAILURE(registerModel(scene, modelPath));
    }

    edits::EditHistory history;
    stageUndo(history, anchor,
              edits::EditCommand::removeAsset(someOtherUUID(), AssetType::Model, modelPath, "", "", ""));
    expectUndo(scene, &scene.assetRegistry, history, readded ? HistoryResult::targetChanged : HistoryResult::applied,
               someOtherUUID());
  }
}

TEST(EditHistoryRefusalMatrix, UndoOfARenameAssetNeedsTheRecordedAfterName)
{
  for (std::size_t variant = 0; variant < controlMovedGone.size(); ++variant)
  {
    SCOPED_TRACE(variant);
    AssetScene scene;
    const auto anchor = addObject(scene, "Object");
    if (variant != 2)
    {
      ASSERT_NO_FATAL_FAILURE(registerModel(scene, modelPath, "", variant == 1 ? "Other" : "New"));
    }

    edits::EditHistory history;
    stageUndo(history, anchor, edits::EditCommand::renameAsset(someOtherUUID(), "Old", "New"));
    expectUndo(scene, &scene.assetRegistry, history, controlMovedGone[variant], someOtherUUID());
  }
}

TEST(EditHistoryRefusalMatrix, RedoOfARenameAssetNeedsTheRecordedBeforeName)
{
  for (const bool renamedElsewhere : { false, true })
  {
    SCOPED_TRACE(renamedElsewhere);
    AssetScene scene;
    const auto anchor = addObject(scene, "Object");
    ASSERT_NO_FATAL_FAILURE(registerModel(scene, modelPath, "", "New"));

    edits::EditHistory history;
    ASSERT_NO_FATAL_FAILURE(stageRedo(scene, &scene.assetRegistry, history, anchor,
                                      edits::EditCommand::renameAsset(someOtherUUID(), "Old", "New")));

    scene.assetRegistry.renameAsset(someOtherUUID(), renamedElsewhere ? "Other" : "Old");
    expectRedo(scene, &scene.assetRegistry, history,
               renamedElsewhere ? HistoryResult::targetChanged : HistoryResult::applied, someOtherUUID());
  }
}

TEST(EditHistoryRefusalMatrix, RedoOfAnAddAssetNeedsTheUUIDFreeAgain)
{
  for (const bool stillThere : { false, true })
  {
    SCOPED_TRACE(stillThere);
    AssetScene scene;
    const auto anchor = addObject(scene, "Object");
    ASSERT_NO_FATAL_FAILURE(registerModel(scene, modelPath));

    edits::EditHistory history;
    ASSERT_NO_FATAL_FAILURE(stageRedo(scene, &scene.assetRegistry, history, anchor,
      edits::EditCommand::addAsset(someOtherUUID(), AssetType::Model, modelPath, "", "")));

    if (!stillThere)
    {
      scene.assetRegistry.removeAsset(someOtherUUID());
    }
    expectRedo(scene, &scene.assetRegistry, history,
               stillThere ? HistoryResult::targetChanged : HistoryResult::applied, someOtherUUID());
  }
}

// What the registry holds must match the removed record field by field, not just exist.
TEST(EditHistoryRefusalMatrix, RedoOfARemoveAssetNeedsTheRecordToMatchEveryRecordedField)
{
  enum Variant { control, otherPath, otherClass, otherName };

  for (const int variant : { control, otherPath, otherClass, otherName })
  {
    SCOPED_TRACE(variant);
    AssetScene scene;
    const auto anchor = addObject(scene, "Object");

    edits::EditHistory history;
    ASSERT_NO_FATAL_FAILURE(stageRedo(scene, &scene.assetRegistry, history, anchor,
      edits::EditCommand::removeAsset(someOtherUUID(), AssetType::Model, modelPath, "Cls", "", "Disp")));

    ASSERT_NO_FATAL_FAILURE(registerModel(scene, variant == otherPath ? "models/b.obj" : modelPath,
                                          variant == otherClass ? "Other" : "Cls",
                                          variant == otherName ? "Other" : "Disp"));
    expectRedo(scene, &scene.assetRegistry, history,
               variant == control ? HistoryResult::applied : HistoryResult::targetChanged, someOtherUUID());
  }
}

// Only the path or only the class differs; the body is the same either way.
TEST(EditHistoryRefusalMatrix, ReplaceAssetUndoAndRedoCompareThePathAndTheClassToo)
{
  enum Variant { control, otherPath, otherClass };
  const auto replace = edits::EditCommand::replaceAsset(someOtherUUID(), AssetType::Model, "models/a.obj", "ClsA",
                                                         "", "models/b.obj", "ClsB", "");

  for (const int variant : { control, otherPath, otherClass })
  {
    SCOPED_TRACE(variant);
    AssetScene scene;
    const auto anchor = addObject(scene, "Object");
    ASSERT_NO_FATAL_FAILURE(registerModel(scene, variant == otherPath ? "models/c.obj" : "models/b.obj",
                                          variant == otherClass ? "ClsC" : "ClsB"));

    edits::EditHistory history;
    stageUndo(history, anchor, replace);
    expectUndo(scene, &scene.assetRegistry, history,
               variant == control ? HistoryResult::applied : HistoryResult::targetChanged, someOtherUUID());
  }

  for (const int variant : { control, otherPath, otherClass })
  {
    SCOPED_TRACE(variant);
    AssetScene scene;
    const auto anchor = addObject(scene, "Object");
    ASSERT_NO_FATAL_FAILURE(registerModel(scene, "models/b.obj", "ClsB"));

    edits::EditHistory history;
    ASSERT_NO_FATAL_FAILURE(stageRedo(scene, &scene.assetRegistry, history, anchor, replace));

    scene.assetRegistry.removeAsset(someOtherUUID());
    ASSERT_NO_FATAL_FAILURE(registerModel(scene, variant == otherPath ? "models/c.obj" : "models/a.obj",
                                          variant == otherClass ? "ClsC" : "ClsA"));
    expectRedo(scene, &scene.assetRegistry, history,
               variant == control ? HistoryResult::applied : HistoryResult::targetChanged, someOtherUUID());
  }
}
