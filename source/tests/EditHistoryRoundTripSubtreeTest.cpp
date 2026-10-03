#include <gtest/gtest.h>

#include "TestScene.h"
#include "Replication.h"
#include "edits/EditCommand.h"
#include "edits/EditHistory.h"
#include "scenes/SceneManager.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
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
