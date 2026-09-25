#include <gtest/gtest.h>

#include "TestScene.h"
#include "Replication.h"
#include "edits/EditCommand.h"
#include "edits/EditHistory.h"
#include "edits/RecordEdits.h"
#include "scenes/SceneManager.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Script.h"
#include "objects/components/Transform.h"
#include "assets/AssetRegistry.h"
#include "EditHistoryFixtures.h"

#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <vector>

namespace {
  using namespace editHistoryFixtures;
}

// --- Round trips: undo restores the before state, redo restores the after state, both via the exact
// applySceneEdit/applyComponentEdit path the authoritative server would run. These also serve as the
// positive controls for the refusal tests in EditHistoryRefusalTest.cpp, which cover the same kinds under
// conflict.

TEST(EditHistory, UndoAndRedoRoundTripAComponentEdit)
{
  auto scene = makeScene();
  const auto transform = transformOf(scene.object);

  transform->setPosition({ 1, 2, 3 });
  const auto before = transform->serialize();
  transform->setPosition({ 4, 5, 6 });
  const auto after = transform->serialize();

  edits::EditHistory history;
  history.record(edits::EditCommand::componentEdit(scene.object->getUUID(), before, after));

  ASSERT_TRUE(history.canUndo());
  const auto undoOutcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_TRUE(undoOutcome.messagePayload.has_value());
  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager, *undoOutcome.messagePayload),
            replication::ComponentEditResult::applied);
  expectNear(transform->getLocalPosition(), { 1, 2, 3 });

  ASSERT_TRUE(history.canRedo());
  const auto redoOutcome = history.redo(*scene.objectManager);
  ASSERT_TRUE(redoOutcome.ok());
  ASSERT_TRUE(redoOutcome.messagePayload.has_value());
  EXPECT_EQ(replication::applyComponentEdit(*scene.objectManager, *redoOutcome.messagePayload),
            replication::ComponentEditResult::applied);
  expectNear(transform->getLocalPosition(), { 4, 5, 6 });
}

TEST(EditHistory, UndoAndRedoRoundTripAnAddObject)
{
  auto scene = makeScene();

  const auto added = std::make_shared<Object>("Added");
  scene.objectManager->addObject(added);
  ASSERT_EQ(scene.objectManager->getObjects().size(), 2u);

  edits::EditHistory history;
  history.record(edits::EditCommand::addObject(added->getUUID(), std::nullopt, "Added", 1));

  const auto undoOutcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_TRUE(undoOutcome.jsonPayload.has_value());
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *undoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_EQ(scene.objectManager->getObjects().size(), 1u);

  const auto redoOutcome = history.redo(*scene.objectManager);
  ASSERT_TRUE(redoOutcome.ok());
  ASSERT_TRUE(redoOutcome.jsonPayload.has_value());
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *redoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_EQ(scene.objectManager->getObjects().size(), 2u);

  // The redone object must come back with the uuid the command already recorded - the entry undo() just
  // moved onto the undo stack still names added->getUUID(), so a server-minted uuid here would leave that
  // entry targeting an object the scene never actually lost, and the next undo would refuse with
  // targetMissing against a live object instead of removing it.
  const auto redone = scene.objectManager->getObjectByUUID(added->getUUID());
  ASSERT_NE(redone, nullptr);

  const auto secondUndoOutcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(secondUndoOutcome.ok());
  ASSERT_TRUE(secondUndoOutcome.jsonPayload.has_value());
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *secondUndoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_EQ(scene.objectManager->getObjects().size(), 1u);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(added->getUUID()), nullptr);
}

TEST(EditHistory, UndoAndRedoRoundTripAReparent)
{
  auto scene = makeScene();
  const auto child = addObject(scene, "Child");
  const auto parentUUID = scene.object->getUUID();

  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
              replication::buildReparentObject(child->getUUID(), &parentUUID)),
            replication::SceneEditResult::applied);
  ASSERT_EQ(child->getParent(), scene.object);

  edits::EditHistory history;
  history.record(edits::EditCommand::reparentObject(child->getUUID(), std::nullopt, scene.object->getUUID()));

  const auto undoOutcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_TRUE(undoOutcome.jsonPayload.has_value());
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *undoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_EQ(child->getParent(), nullptr);

  const auto redoOutcome = history.redo(*scene.objectManager);
  ASSERT_TRUE(redoOutcome.ok());
  ASSERT_TRUE(redoOutcome.jsonPayload.has_value());
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *redoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_EQ(child->getParent(), scene.object);
}

TEST(EditHistory, UndoAndRedoRoundTripASameParentReorderInBothDirections)
{
  auto scene = makeScene();
  const auto a = addChildObject(scene, "A", scene.object);
  const auto b = addChildObject(scene, "B", scene.object);
  const auto c = addChildObject(scene, "C", scene.object);
  // scene.object's children start as [A, B, C].

  const auto parentUUID = scene.object->getUUID();

  // Down: move A (index 0) past the end - the post-removal list is [B, C], so index 2 appends it.
  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
              replication::buildReorderObject(a->getUUID(), &parentUUID, 2)),
            replication::SceneEditResult::applied);
  ASSERT_EQ(scene.object->getChildren(), (std::vector<std::shared_ptr<Object>>{ b, c, a }));

  edits::EditHistory history;
  history.record(edits::EditCommand::reorderObject(a->getUUID(), parentUUID, 0, parentUUID, 2));

  auto undoOutcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_TRUE(undoOutcome.jsonPayload.has_value());
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *undoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_EQ(scene.object->getChildren(), (std::vector<std::shared_ptr<Object>>{ a, b, c }));

  auto redoOutcome = history.redo(*scene.objectManager);
  ASSERT_TRUE(redoOutcome.ok());
  ASSERT_TRUE(redoOutcome.jsonPayload.has_value());
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *redoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_EQ(scene.object->getChildren(), (std::vector<std::shared_ptr<Object>>{ b, c, a }));

  // Up: move A (now index 2) back to the front - its own round trip through a freshly recorded command.
  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
              replication::buildReorderObject(a->getUUID(), &parentUUID, 0)),
            replication::SceneEditResult::applied);
  ASSERT_EQ(scene.object->getChildren(), (std::vector<std::shared_ptr<Object>>{ a, b, c }));

  history.record(edits::EditCommand::reorderObject(a->getUUID(), parentUUID, 2, parentUUID, 0));

  undoOutcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_TRUE(undoOutcome.jsonPayload.has_value());
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *undoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_EQ(scene.object->getChildren(), (std::vector<std::shared_ptr<Object>>{ b, c, a }));

  redoOutcome = history.redo(*scene.objectManager);
  ASSERT_TRUE(redoOutcome.ok());
  ASSERT_TRUE(redoOutcome.jsonPayload.has_value());
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *redoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_EQ(scene.object->getChildren(), (std::vector<std::shared_ptr<Object>>{ a, b, c }));
}

TEST(EditHistory, UndoAndRedoRoundTripAReorderToADifferentParentAtAnIndex)
{
  auto scene = makeScene();
  const auto sourceParent = addObject(scene, "SourceParent");
  const auto destParent = addObject(scene, "DestParent");
  const auto moved = addChildObject(scene, "Moved", sourceParent);
  const auto destA = addChildObject(scene, "DestA", destParent);
  const auto destB = addChildObject(scene, "DestB", destParent);
  // destParent's children start as [DestA, DestB]; moved sits alone under sourceParent.

  const auto destUUID = destParent->getUUID();
  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
              replication::buildReorderObject(moved->getUUID(), &destUUID, 1)),
            replication::SceneEditResult::applied);
  ASSERT_EQ(moved->getParent(), destParent);
  ASSERT_EQ(destParent->getChildren(), (std::vector<std::shared_ptr<Object>>{ destA, moved, destB }));
  ASSERT_TRUE(sourceParent->getChildren().empty());

  edits::EditHistory history;
  // Before: moved sat at index 0 under sourceParent. After: index 1 under destParent.
  history.record(edits::EditCommand::reorderObject(moved->getUUID(), sourceParent->getUUID(), 0,
                                                    destParent->getUUID(), 1));

  const auto undoOutcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_TRUE(undoOutcome.jsonPayload.has_value());
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *undoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_EQ(moved->getParent(), sourceParent);
  EXPECT_EQ(sourceParent->getChildren(), (std::vector<std::shared_ptr<Object>>{ moved }));
  EXPECT_EQ(destParent->getChildren(), (std::vector<std::shared_ptr<Object>>{ destA, destB }));

  const auto redoOutcome = history.redo(*scene.objectManager);
  ASSERT_TRUE(redoOutcome.ok());
  ASSERT_TRUE(redoOutcome.jsonPayload.has_value());
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *redoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_EQ(moved->getParent(), destParent);
  EXPECT_EQ(destParent->getChildren(), (std::vector<std::shared_ptr<Object>>{ destA, moved, destB }));
  EXPECT_TRUE(sourceParent->getChildren().empty());
}

// Same shape as the other kinds' target-changed refusals: something else moves the object between record
// and undo, so the live position no longer matches the "after" state this entry expects.
TEST(EditHistory, UndoOfAReorderRefusesWhenTheObjectsPositionChangedUnderneath)
{
  auto scene = makeScene();
  const auto a = addChildObject(scene, "A", scene.object);
  const auto b = addChildObject(scene, "B", scene.object);
  const auto c = addChildObject(scene, "C", scene.object);
  // scene.object's children start as [A, B, C].

  const auto parentUUID = scene.object->getUUID();

  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
              replication::buildReorderObject(a->getUUID(), &parentUUID, 2)),
            replication::SceneEditResult::applied);
  // children: [B, C, A]

  edits::EditHistory history;
  history.record(edits::EditCommand::reorderObject(a->getUUID(), parentUUID, 0, parentUUID, 2));

  // Something else reorders A again before undo runs, moving it away from the slot this entry expects.
  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
              replication::buildReorderObject(a->getUUID(), &parentUUID, 0)),
            replication::SceneEditResult::applied);
  // children: [A, B, C]

  const auto outcome = history.undo(*scene.objectManager);
  EXPECT_EQ(outcome.result, edits::HistoryResult::targetChanged);
  ASSERT_TRUE(outcome.conflict.has_value());
  EXPECT_EQ(*outcome.conflict, a->getUUID());
  EXPECT_FALSE(history.canUndo());

  // The interference is left exactly as it was - the refused undo touched nothing.
  EXPECT_EQ(scene.object->getChildren(), (std::vector<std::shared_ptr<Object>>{ a, b, c }));
}

TEST(EditHistory, UndoAndRedoRoundTripARename)
{
  auto scene = makeScene();

  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
              replication::buildRenameObject(scene.object->getUUID(), "Renamed")),
            replication::SceneEditResult::applied);

  edits::EditHistory history;
  history.record(edits::EditCommand::renameObject(scene.object->getUUID(), "Object", "Renamed"));

  const auto undoOutcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_TRUE(undoOutcome.jsonPayload.has_value());
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *undoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_EQ(scene.object->getName(), "Object");

  const auto redoOutcome = history.redo(*scene.objectManager);
  ASSERT_TRUE(redoOutcome.ok());
  ASSERT_TRUE(redoOutcome.jsonPayload.has_value());
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *redoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_EQ(scene.object->getName(), "Renamed");
}

TEST(EditHistory, UndoAndRedoRoundTripAnAddComponent)
{
  auto scene = makeScene();

  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
              replication::buildAddComponent(scene.object->getUUID(), "RigidBody")),
            replication::SceneEditResult::applied);
  ASSERT_TRUE(scene.object->getComponents().contains(ComponentType::rigidBody));

  edits::EditHistory history;
  history.record(edits::EditCommand::addComponent(scene.object->getUUID(), "RigidBody"));

  const auto undoOutcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_TRUE(undoOutcome.jsonPayload.has_value());
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *undoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_FALSE(scene.object->getComponents().contains(ComponentType::rigidBody));

  const auto redoOutcome = history.redo(*scene.objectManager);
  ASSERT_TRUE(redoOutcome.ok());
  ASSERT_TRUE(redoOutcome.jsonPayload.has_value());
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *redoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_TRUE(scene.object->getComponents().contains(ComponentType::rigidBody));
}

// buildRedoJSON's addComponent case branches on registryKey == "Script" (buildAddScript) versus every
// other key (buildAddComponent) - the RigidBody test above only exercises the latter, so this covers the
// Script branch specifically, plus a redo refusal that only that branch's className identity can produce.
TEST(EditHistory, UndoAndRedoRoundTripAnAddScriptComponent)
{
  auto scene = makeScene();

  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
              replication::buildAddScript(scene.object->getUUID(), "PlayerScript")),
            replication::SceneEditResult::applied);
  ASSERT_TRUE(hasScript(scene.object, "PlayerScript"));

  edits::EditHistory history;
  history.record(edits::EditCommand::addComponent(scene.object->getUUID(), "Script", "PlayerScript"));

  const auto undoOutcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_TRUE(undoOutcome.jsonPayload.has_value());
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *undoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_FALSE(hasScript(scene.object, "PlayerScript"));
  ASSERT_TRUE(history.canRedo());

  // Positive control: with nothing interfering, redo restores it through buildAddScript.
  const auto redoOutcome = history.redo(*scene.objectManager);
  ASSERT_TRUE(redoOutcome.ok());
  ASSERT_TRUE(redoOutcome.jsonPayload.has_value());
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *redoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_TRUE(hasScript(scene.object, "PlayerScript"));
  EXPECT_EQ(scene.object->getScripts().size(), 1u);
}

TEST(EditHistory, RedoOfAnAddScriptComponentRefusesWhenTheClassAlreadyExists)
{
  auto scene = makeScene();

  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
              replication::buildAddScript(scene.object->getUUID(), "PlayerScript")),
            replication::SceneEditResult::applied);

  edits::EditHistory history;
  history.record(edits::EditCommand::addComponent(scene.object->getUUID(), "Script", "PlayerScript"));

  const auto undoOutcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(undoOutcome.ok());
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *undoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  ASSERT_TRUE(history.canRedo());

  // Something else adds a script under the same class name before redo runs.
  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
              replication::buildAddScript(scene.object->getUUID(), "PlayerScript")),
            replication::SceneEditResult::applied);

  const auto redoOutcome = history.redo(*scene.objectManager);
  EXPECT_EQ(redoOutcome.result, edits::HistoryResult::targetChanged);
  ASSERT_TRUE(redoOutcome.conflict.has_value());
  EXPECT_EQ(*redoOutcome.conflict, scene.object->getUUID());
  EXPECT_FALSE(history.canRedo());

  // Only one script survives either way - the refusal did not double up on the class.
  EXPECT_EQ(scene.object->getScripts().size(), 1u);
}

TEST(EditHistory, UndoAndRedoRoundTripARemoveComponent)
{
  auto scene = makeScene();

  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
              replication::buildAddComponent(scene.object->getUUID(), "RigidBody")),
            replication::SceneEditResult::applied);

  const auto rigidBody = std::dynamic_pointer_cast<RigidBody>(
    scene.object->getComponents().at(ComponentType::rigidBody));
  ASSERT_TRUE(rigidBody);
  rigidBody->setMass(7.5f); // distinguishes the restored component from a fresh default

  // Captured before the removal, the way commandForRemoveComponent captures it from the pre-edit view.
  const auto removedJSON = rigidBody->serialize();

  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
              replication::buildRemoveComponent(scene.object->getUUID(), rigidBody)),
            replication::SceneEditResult::applied);
  ASSERT_FALSE(scene.object->getComponents().contains(ComponentType::rigidBody));

  edits::EditHistory history;
  history.record(edits::EditCommand::removeComponent(scene.object->getUUID(), removedJSON));

  const auto undoOutcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_TRUE(undoOutcome.jsonPayload.has_value());
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *undoOutcome.jsonPayload),
            replication::SceneEditResult::applied);

  const auto restored = std::dynamic_pointer_cast<RigidBody>(
    scene.object->getComponents().at(ComponentType::rigidBody));
  ASSERT_TRUE(restored);
  EXPECT_NEAR(restored->getMass(), 7.5f, 1e-5f);
  EXPECT_EQ(restored->serialize(), removedJSON);

  const auto redoOutcome = history.redo(*scene.objectManager);
  ASSERT_TRUE(redoOutcome.ok());
  ASSERT_TRUE(redoOutcome.jsonPayload.has_value());
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *redoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_FALSE(scene.object->getComponents().contains(ComponentType::rigidBody));
}

namespace {
  // Parent's children so a removeObject round trip can check them by name: A/B never move, X is what gets
  // deleted and undone, C1 is X's own child (the one deleteObjectsMarkedForDeletion promotes to Parent).
  struct RemovedX {
    std::shared_ptr<Object> parent;
    std::shared_ptr<Object> a;
    std::shared_ptr<Object> b;
    std::shared_ptr<Object> c1;
    uuids::uuid xUUID;
  };

  // Builds Parent with children [A, X, B] (X with its own child C1), records the removeObject command from
  // X's pre-removal state, and applies the removal for real - the shared setup every removeObject round
  // trip / refusal test below needs. X sits off-origin so promoting C1 to Parent actually rewrites its
  // local values, which is what proves undo restores the recorded pre-removal transform rather than
  // whatever WorldPlacement left it at.
  RemovedX buildAndRemoveX(editHistoryFixtures::Scene& scene, edits::EditHistory& history)
  {
    RemovedX removed;
    removed.parent = addObject(scene, "Parent");
    removed.a = addChildObject(scene, "A", removed.parent);
    const auto x = addChildObject(scene, "X", removed.parent);
    removed.b = addChildObject(scene, "B", removed.parent);
    removed.c1 = addChildObject(scene, "C1", x);

    transformOf(x)->setPosition({ 10, 0, 0 });
    transformOf(removed.c1)->setPosition({ 1, 2, 3 });

    removed.xUUID = x->getUUID();
    const auto parentUUID = removed.parent->getUUID();
    history.record(edits::EditCommand::removeObject(removed.xUUID, parentUUID, 1, x->serialize()));

    EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, replication::buildRemoveObject(removed.xUUID)),
              replication::SceneEditResult::applied);
    EXPECT_EQ(scene.objectManager->getObjectByUUID(removed.xUUID), nullptr);
    EXPECT_EQ(removed.parent->getChildren(),
              (std::vector<std::shared_ptr<Object>>{ removed.a, removed.c1, removed.b }));
    EXPECT_EQ(removed.c1->getParent(), removed.parent);

    return removed;
  }

  // X is back at its old index between A and B, C1 is its child again at its recorded pre-removal local
  // transform - what both undos below (the first, and the one after a redo) need to check.
  void expectXRestoredWithC1(const RemovedX& removed)
  {
    ASSERT_EQ(removed.parent->getChildren().size(), 3u);
    const auto restoredX = removed.parent->getChildren()[1];
    EXPECT_EQ(restoredX->getUUID(), removed.xUUID);
    EXPECT_EQ(restoredX->getName(), "X");
    EXPECT_EQ(removed.parent->getChildren()[0], removed.a);
    EXPECT_EQ(removed.parent->getChildren()[2], removed.b);
    ASSERT_EQ(restoredX->getChildren().size(), 1u);
    EXPECT_EQ(restoredX->getChildren().front(), removed.c1);
    EXPECT_EQ(removed.c1->getParent(), restoredX);
    expectNear(transformOf(removed.c1)->getLocalPosition(), { 1, 2, 3 });
    expectNear(transformOf(restoredX)->getLocalPosition(), { 10, 0, 0 });
  }
}

// removeObject undoes through restoreObject's "adopt" field: deleteObjectsMarkedForDeletion promotes the
// removed object's direct children up to its own parent rather than deleting them, so undo has to both
// recreate the removed object AND reclaim those still-live children back under it - see AGENTS.md's Editor
// Undo/Redo section and EditCommand.h's isReversible.
TEST(EditHistory, UndoAndRedoRoundTripARemoveObjectWithAPromotedChild)
{
  auto scene = makeScene();
  edits::EditHistory history;
  const auto removed = buildAndRemoveX(scene, history);

  const auto undoOutcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_TRUE(undoOutcome.jsonPayload.has_value());
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *undoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  expectXRestoredWithC1(removed);

  // Redo: deleted again, C1 promoted back to Parent exactly as the first removal left it.
  const auto redoOutcome = history.redo(*scene.objectManager);
  ASSERT_TRUE(redoOutcome.ok());
  ASSERT_TRUE(redoOutcome.jsonPayload.has_value());
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *redoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(removed.xUUID), nullptr);
  EXPECT_EQ(removed.parent->getChildren(),
            (std::vector<std::shared_ptr<Object>>{ removed.a, removed.c1, removed.b }));

  // Undo again: the round trip still works a second time.
  const auto secondUndoOutcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(secondUndoOutcome.ok());
  ASSERT_TRUE(secondUndoOutcome.jsonPayload.has_value());
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *secondUndoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  expectXRestoredWithC1(removed);
}

namespace {
  // Shared shape assertion for the duplicateObject/instantiatePrefab round trips below: one child at the
  // recorded local position, carrying a RigidBody with the recorded mass - a non-Transform component,
  // looked up by ComponentType (never positionally), to prove component data survives the round trip too,
  // not just the transform every other structural round trip already covers.
  void expectSubtreeShape(const std::shared_ptr<Object>& root, const glm::vec3& rootPosition,
                          const glm::vec3& childPosition, float childMass)
  {
    ASSERT_TRUE(root);
    ASSERT_EQ(root->getChildren().size(), 1u);
    expectNear(transformOf(root)->getLocalPosition(), rootPosition);

    const auto& child = root->getChildren().front();
    expectNear(transformOf(child)->getLocalPosition(), childPosition);

    const auto rigidBody = std::dynamic_pointer_cast<RigidBody>(
      child->getComponents().at(ComponentType::rigidBody));
    ASSERT_TRUE(rigidBody);
    EXPECT_NEAR(rigidBody->getMass(), childMass, 1e-5f);
  }
}

TEST(EditHistory, UndoAndRedoRoundTripADuplicateObjectWithAChildAndANonIdentityTransform)
{
  auto scene = makeScene();
  const auto source = addObject(scene, "Source", { 1, 2, 3 }, { 2, 2, 2 });
  const auto child = addChildObject(scene, "Child", source);
  transformOf(child)->setPosition({ 5, 6, 7 });
  fixtures::addRigidBody(child)->setMass(9.5f);

  const auto duplicateUUID = someOtherUUID();
  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
              replication::buildDuplicateObject(source->getUUID(), &duplicateUUID)),
            replication::SceneEditResult::applied);
  ASSERT_NO_FATAL_FAILURE(expectSubtreeShape(scene.objectManager->getObjectByUUID(duplicateUUID),
                                             { 1, 2, 3 }, { 5, 6, 7 }, 9.5f));

  edits::EditHistory history;
  history.record(edits::EditCommand::duplicateObject(source->getUUID(), duplicateUUID, std::nullopt, 1));

  const auto undoOutcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(undoOutcome.ok());
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *undoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_FALSE(scene.objectManager->getObjectByUUID(duplicateUUID));
  // The original and its child are untouched - removeSubtree only ever took the duplicate's own subtree.
  EXPECT_EQ(source->getChildren().size(), 1u);
  EXPECT_EQ(source->getChildren().front(), child);

  const auto redoOutcome = history.redo(*scene.objectManager);
  ASSERT_TRUE(redoOutcome.ok());
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *redoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  ASSERT_NO_FATAL_FAILURE(expectSubtreeShape(scene.objectManager->getObjectByUUID(duplicateUUID),
                                             { 1, 2, 3 }, { 5, 6, 7 }, 9.5f));
}

TEST(EditHistory, UndoAndRedoRoundTripAnInstantiatePrefabWithAChildAndANonIdentityTransform)
{
  // The prefab body: built on a throwaway scene, exactly the shape AssetRegistry::getPrefabBody hands
  // applySceneEdit - a serialized subtree with its own (soon-to-be-discarded) uuids.
  nlohmann::json body;
  {
    auto bodyScene = fixtures::makeScene();
    const auto root = addObject(bodyScene, "Prefab", { 1, 2, 3 });
    const auto child = addChildObject(bodyScene, "PrefabChild", root);
    transformOf(child)->setPosition({ 5, 6, 7 });
    fixtures::addRigidBody(child)->setMass(4.25f);
    body = root->serialize();
  }

  AssetScene scene;
  const auto prefabUUID = someOtherUUID();
  scene.assetRegistry.registerAsset({ .uuid = prefabUUID, .type = AssetType::Prefab,
                                      .path = "TestPrefab", .body = body.dump() });

  const auto instanceUUID = anotherUUID();
  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
              replication::buildInstantiatePrefab(prefabUUID, nullptr, &instanceUUID),
              &scene.assetRegistry),
            replication::SceneEditResult::applied);
  ASSERT_NO_FATAL_FAILURE(expectSubtreeShape(scene.objectManager->getObjectByUUID(instanceUUID),
                                             { 1, 2, 3 }, { 5, 6, 7 }, 4.25f));

  edits::EditHistory history;
  history.record(
    edits::EditCommand::instantiatePrefab(prefabUUID, instanceUUID, std::nullopt, 0, body.dump()));

  const auto undoOutcome = history.undo(*scene.objectManager, &scene.assetRegistry);
  ASSERT_TRUE(undoOutcome.ok());
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *undoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_FALSE(scene.objectManager->getObjectByUUID(instanceUUID));

  const auto redoOutcome = history.redo(*scene.objectManager, &scene.assetRegistry);
  ASSERT_TRUE(redoOutcome.ok());
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *redoOutcome.jsonPayload,
              &scene.assetRegistry),
            replication::SceneEditResult::applied);
  ASSERT_NO_FATAL_FAILURE(expectSubtreeShape(scene.objectManager->getObjectByUUID(instanceUUID),
                                             { 1, 2, 3 }, { 5, 6, 7 }, 4.25f));
}

TEST(EditHistory, UndoAndRedoRoundTripAnAddAsset)
{
  AssetScene scene;
  SceneManager sceneManager;
  const auto assetUUID = someOtherUUID();

  scene.assetRegistry.registerAsset({ .uuid = assetUUID, .type = AssetType::Model, .path = "models/thing.obj" });
  ASSERT_NE(scene.assetRegistry.getByUUID(assetUUID), nullptr);

  edits::EditHistory history;
  history.record(edits::EditCommand::addAsset(assetUUID, AssetType::Model, "models/thing.obj", "", ""));

  const auto undoOutcome = history.undo(*scene.objectManager, &scene.assetRegistry);
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_TRUE(undoOutcome.messagePayload.has_value());
  replication::applyRemoveAsset(scene.assetRegistry, replication::unpackRemoveAsset(*undoOutcome.messagePayload));
  EXPECT_EQ(scene.assetRegistry.getByUUID(assetUUID), nullptr);

  const auto redoOutcome = history.redo(*scene.objectManager, &scene.assetRegistry);
  ASSERT_TRUE(redoOutcome.ok());
  ASSERT_TRUE(redoOutcome.messagePayload.has_value());
  replication::applyAddAsset(scene.assetRegistry, sceneManager, scene.componentRegistry,
                             replication::unpackAddAsset(*redoOutcome.messagePayload));
  ASSERT_NE(scene.assetRegistry.getByUUID(assetUUID), nullptr);
  EXPECT_EQ(scene.assetRegistry.getByUUID(assetUUID)->path, "models/thing.obj");
}

TEST(EditHistory, UndoAndRedoRoundTripARenameAsset)
{
  AssetScene scene;
  const auto assetUUID = someOtherUUID();

  scene.assetRegistry.registerAsset({ .uuid = assetUUID, .type = AssetType::Model, .path = "models/thing.obj" });
  scene.assetRegistry.renameAsset(assetUUID, "New Name");

  edits::EditHistory history;
  history.record(edits::EditCommand::renameAsset(assetUUID, "", "New Name"));

  const auto undoOutcome = history.undo(*scene.objectManager, &scene.assetRegistry);
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_TRUE(undoOutcome.messagePayload.has_value());
  replication::applyRenameAsset(scene.assetRegistry, replication::unpackRenameAsset(*undoOutcome.messagePayload));
  EXPECT_EQ(scene.assetRegistry.getByUUID(assetUUID)->displayName, "");

  const auto redoOutcome = history.redo(*scene.objectManager, &scene.assetRegistry);
  ASSERT_TRUE(redoOutcome.ok());
  ASSERT_TRUE(redoOutcome.messagePayload.has_value());
  replication::applyRenameAsset(scene.assetRegistry, replication::unpackRenameAsset(*redoOutcome.messagePayload));
  EXPECT_EQ(scene.assetRegistry.getByUUID(assetUUID)->displayName, "New Name");
}

TEST(EditHistory, UndoAndRedoRoundTripARemoveAsset)
{
  AssetScene scene;
  SceneManager sceneManager;
  const auto assetUUID = someOtherUUID();

  scene.assetRegistry.registerAsset({ .uuid = assetUUID, .type = AssetType::Model, .path = "models/thing.obj" });
  replication::applyRemoveAsset(scene.assetRegistry, replication::buildRemoveAsset(assetUUID));
  ASSERT_EQ(scene.assetRegistry.getByUUID(assetUUID), nullptr);

  edits::EditHistory history;
  history.record(edits::EditCommand::removeAsset(assetUUID, AssetType::Model, "models/thing.obj", "", "", ""));

  const auto undoOutcome = history.undo(*scene.objectManager, &scene.assetRegistry);
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_TRUE(undoOutcome.messagePayload.has_value());
  replication::applyAddAsset(scene.assetRegistry, sceneManager, scene.componentRegistry,
                             replication::unpackAddAsset(*undoOutcome.messagePayload));
  EXPECT_NE(scene.assetRegistry.getByUUID(assetUUID), nullptr);

  const auto redoOutcome = history.redo(*scene.objectManager, &scene.assetRegistry);
  ASSERT_TRUE(redoOutcome.ok());
  ASSERT_TRUE(redoOutcome.messagePayload.has_value());
  replication::applyRemoveAsset(scene.assetRegistry, replication::unpackRemoveAsset(*redoOutcome.messagePayload));
  EXPECT_EQ(scene.assetRegistry.getByUUID(assetUUID), nullptr);
}

// A deleted asset's undo restores its whole record, including a rename override set before the deletion -
// the display name is part of "the record", not just the file/path/body an addAsset without it would
// leave the asset under its derived name.
TEST(EditHistory, UndoOfARemoveAssetRestoresTheDisplayName)
{
  AssetScene scene;
  SceneManager sceneManager;
  const auto assetUUID = someOtherUUID();

  scene.assetRegistry.registerAsset({ .uuid = assetUUID, .type = AssetType::Model, .path = "models/rock.obj" });
  scene.assetRegistry.renameAsset(assetUUID, "Rock");
  ASSERT_EQ(scene.assetRegistry.getByUUID(assetUUID)->displayName, "Rock");

  const auto removeCommand = edits::commandForRemoveAsset(replication::buildRemoveAsset(assetUUID),
                                                           scene.assetRegistry);
  ASSERT_TRUE(removeCommand.has_value());

  replication::applyRemoveAsset(scene.assetRegistry, replication::buildRemoveAsset(assetUUID));
  ASSERT_EQ(scene.assetRegistry.getByUUID(assetUUID), nullptr);

  edits::EditHistory history;
  history.record(*removeCommand);

  const auto undoOutcome = history.undo(*scene.objectManager, &scene.assetRegistry);
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_TRUE(undoOutcome.messagePayload.has_value());
  replication::applyAddAsset(scene.assetRegistry, sceneManager, scene.componentRegistry,
                             replication::unpackAddAsset(*undoOutcome.messagePayload));

  const auto* restored = scene.assetRegistry.getByUUID(assetUUID);
  ASSERT_NE(restored, nullptr);
  EXPECT_EQ(restored->uuid, assetUUID);
  EXPECT_EQ(restored->path, "models/rock.obj");
  EXPECT_EQ(restored->displayName, "Rock");

  // Redo validates the live record against what undo just restored (including the display name), so it
  // still succeeds and removes the asset again.
  const auto redoOutcome = history.redo(*scene.objectManager, &scene.assetRegistry);
  ASSERT_TRUE(redoOutcome.ok());
  ASSERT_TRUE(redoOutcome.messagePayload.has_value());
  replication::applyRemoveAsset(scene.assetRegistry, replication::unpackRemoveAsset(*redoOutcome.messagePayload));
  EXPECT_EQ(scene.assetRegistry.getByUUID(assetUUID), nullptr);
}
