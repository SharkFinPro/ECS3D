#include <gtest/gtest.h>

#include "TestScene.h"
#include "Replication.h"
#include "edits/EditCommand.h"
#include "edits/EditHistory.h"
#include "assets/AssetRegistry.h"
#include "scenes/SceneManager.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "objects/components/Transform.h"

#include <glm/vec3.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <uuid.h>

namespace {
  using fixtures::expectNear;
  using fixtures::transformOf;

  // Every object-kind test edits a scene that already holds one object, so the fixture carries one.
  struct Scene : fixtures::Scene {
    std::shared_ptr<Object> object;
  };

  Scene makeScene()
  {
    Scene scene;
    scene.object = addObject(scene, "Object");
    return scene;
  }

  // Asset-kind tests need a registry but not necessarily any objects; still carries an ObjectManager
  // because EditHistory::undo/redo take one unconditionally (unused by asset-kind commands).
  struct AssetScene : fixtures::Scene {
    AssetRegistry assetRegistry;
  };

  uuids::uuid someOtherUUID()
  {
    return uuids::uuid::from_string("123e4567-e89b-12d3-a456-426614174000").value();
  }

  uuids::uuid anotherUUID()
  {
    return uuids::uuid::from_string("00000000-0000-0000-0000-000000000001").value();
  }
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
    const auto transform = transformOf(scene.object);

    transform->setPosition({ 1, 0, 0 });
    const auto before1 = transform->serialize();
    transform->setPosition({ 2, 0, 0 });
    const auto after1 = transform->serialize();

    edits::EditHistory history;
    history.record(edits::EditCommand::componentEdit(scene.object->getUUID(), before1, after1));

    const auto before2 = transform->serialize();
    transform->setPosition({ 4, 0, 0 });
    const auto after2 = transform->serialize();
    history.record(edits::EditCommand::componentEdit(scene.object->getUUID(), before2, after2));

    auto outcome = history.undo(*scene.objectManager);
    ASSERT_TRUE(outcome.ok());
    replication::applyComponentEdit(*scene.objectManager, *outcome.messagePayload);
    outcome = history.undo(*scene.objectManager);
    ASSERT_TRUE(outcome.ok());
    replication::applyComponentEdit(*scene.objectManager, *outcome.messagePayload);

    outcome = history.redo(*scene.objectManager);
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
    const auto transform = transformOf(scene.object);

    transform->setPosition({ 1, 0, 0 });
    const auto before1 = transform->serialize();
    transform->setPosition({ 2, 0, 0 });
    const auto after1 = transform->serialize();

    edits::EditHistory history;
    history.record(edits::EditCommand::componentEdit(scene.object->getUUID(), before1, after1));

    const auto before2 = transform->serialize();
    transform->setPosition({ 4, 0, 0 });
    const auto after2 = transform->serialize();
    history.record(edits::EditCommand::componentEdit(scene.object->getUUID(), before2, after2));

    auto outcome = history.undo(*scene.objectManager);
    ASSERT_TRUE(outcome.ok());
    replication::applyComponentEdit(*scene.objectManager, *outcome.messagePayload);
    outcome = history.undo(*scene.objectManager);
    ASSERT_TRUE(outcome.ok());
    replication::applyComponentEdit(*scene.objectManager, *outcome.messagePayload);

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
