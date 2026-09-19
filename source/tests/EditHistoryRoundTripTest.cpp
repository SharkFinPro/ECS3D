#include <gtest/gtest.h>

#include "TestScene.h"
#include "Replication.h"
#include "edits/EditCommand.h"
#include "edits/EditHistory.h"
#include "scenes/SceneManager.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Script.h"
#include "objects/components/Transform.h"
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
// positive controls for the refusal tests further down, which cover the same kinds under conflict.

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
  history.record(edits::EditCommand::removeAsset(assetUUID, AssetType::Model, "models/thing.obj", "", ""));

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
