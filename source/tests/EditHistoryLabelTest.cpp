#include <gtest/gtest.h>

#include "TestScene.h"
#include "edits/EditCommand.h"
#include "edits/EditHistory.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Transform.h"
#include "assets/AssetRegistry.h"
#include "EditHistoryFixtures.h"

#include <optional>
#include <string>

namespace {
  using namespace editHistoryFixtures;
}

// nextUndoLabel()/nextRedoLabel() are what the Edit menu reads to name the next action ("Undo Rename
// Cube") instead of showing a bare "Undo"/"Redo" - see EditCommand::describeForMenu for the per-kind text.

TEST(EditHistoryLabel, ReturnsNulloptForAnEmptyHistory)
{
  auto scene = makeScene();
  edits::EditHistory history;

  EXPECT_FALSE(history.nextUndoLabel(*scene.objectManager).has_value());
  EXPECT_FALSE(history.nextRedoLabel(*scene.objectManager).has_value());
}

// Positive control for the empty-history case above: once something is recorded, a label comes back.
TEST(EditHistoryLabel, DescribesAComponentEditByItsComponentType)
{
  auto scene = makeScene();
  const auto transform = transformOf(scene.object);

  transform->setPosition({ 1, 0, 0 });
  const auto before = transform->serialize();
  transform->setPosition({ 2, 0, 0 });
  const auto after = transform->serialize();

  edits::EditHistory history;
  history.record(edits::EditCommand::componentEdit(scene.object->getUUID(), before, after));

  const auto label = history.nextUndoLabel(*scene.objectManager);
  ASSERT_TRUE(label.has_value());
  EXPECT_EQ(*label, "Edit Transform");
}

// The command's own recorded afterName is what names the action - not the object's live name, which may
// have moved on since (the same "before/after state as recorded" the validation reads).
TEST(EditHistoryLabel, DescribesARenameByItsRecordedAfterName)
{
  auto scene = makeScene();

  edits::EditHistory history;
  history.record(edits::EditCommand::renameObject(scene.object->getUUID(), "Object", "Renamed"));

  const auto undoLabel = history.nextUndoLabel(*scene.objectManager);
  ASSERT_TRUE(undoLabel.has_value());
  EXPECT_EQ(*undoLabel, "Rename Renamed");

  const auto outcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(outcome.ok());

  // Moved to the redo stack: the same command, so the same label.
  const auto redoLabel = history.nextRedoLabel(*scene.objectManager);
  ASSERT_TRUE(redoLabel.has_value());
  EXPECT_EQ(*redoLabel, "Rename Renamed");
}

// instantiatePrefab records no name of its own (only uuids) - its label has to fall back to resolving the
// prefab's live name out of the asset registry.
TEST(EditHistoryLabel, DescribesAnInstantiatePrefabByResolvingTheLiveAssetName)
{
  auto scene = makeScene();
  AssetRegistry assetRegistry;
  const auto prefabUUID = someOtherUUID();
  const auto instanceUUID = anotherUUID();

  assetRegistry.registerAsset(
    { .uuid = prefabUUID, .type = AssetType::Prefab, .path = "MyPrefab" });

  edits::EditHistory history;
  history.record(edits::EditCommand::instantiatePrefab(prefabUUID, instanceUUID, std::nullopt, 0));

  const auto label = history.nextUndoLabel(*scene.objectManager, &assetRegistry);
  ASSERT_TRUE(label.has_value());
  EXPECT_EQ(*label, "Instantiate MyPrefab");
}

// Same command, but the prefab asset is gone (or no registry was passed at all) by the time the label is
// read - falls back to a generic kind label rather than showing nothing or a raw uuid.
TEST(EditHistoryLabel, DescribesAnInstantiatePrefabWithAGenericFallbackWhenTheAssetCannotBeResolved)
{
  auto scene = makeScene();
  const auto prefabUUID = someOtherUUID();
  const auto instanceUUID = anotherUUID();

  edits::EditHistory history;
  history.record(edits::EditCommand::instantiatePrefab(prefabUUID, instanceUUID, std::nullopt, 0));

  const auto label = history.nextUndoLabel(*scene.objectManager);
  ASSERT_TRUE(label.has_value());
  EXPECT_EQ(*label, "Instantiate Prefab");
}

// removeAsset's label reads directly off what the command itself recorded (its own before-removal path),
// never touching the registry - the asset is gone by the time this is on the stack.
TEST(EditHistoryLabel, DescribesARemoveAssetByItsOwnRecordedPath)
{
  auto scene = makeScene();

  edits::EditHistory history;
  history.record(edits::EditCommand::removeAsset(someOtherUUID(), AssetType::Prefab, "OldPrefab", "", ""));

  const auto label = history.nextUndoLabel(*scene.objectManager);
  ASSERT_TRUE(label.has_value());
  EXPECT_EQ(*label, "Delete Asset OldPrefab");
}
