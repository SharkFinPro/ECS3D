#include <gtest/gtest.h>

#include "TestScene.h"
#include "Replication.h"
#include "edits/EditCommand.h"
#include "edits/EditHistory.h"
#include "edits/RecordEdits.h"
#include "assets/AssetRegistry.h"
#include "scenes/SceneManager.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "objects/components/Script.h"
#include <Protocol.h>
#include "RecordEditsFixtures.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <uuid.h>
#include <vector>

namespace {
  using namespace recordEditsFixtures;
}

// --- Assets: a new uuid is an addition, a uuid the registry already holds is a replacement.

TEST(RecordEdits, AddAssetOverANewUUIDRecordsAnAddAsset)
{
  AssetScene scene;

  const nlohmann::json asset = {
    { "assetType", "model" },
    { "uuid", uuidString(someOtherUUID()) },
    { "path", "models/thing.obj" }
  };

  const auto command = edits::commandForAddAsset(asset, scene.assetRegistry);
  ASSERT_TRUE(command.has_value());
  EXPECT_EQ(*command, edits::EditCommand::addAsset(someOtherUUID(), AssetType::Model,
                                                   "models/thing.obj", "", ""));
}

TEST(RecordEdits, AddAssetOverAnExistingUUIDRecordsAReplaceAssetCarryingTheOldBody)
{
  AssetScene scene;

  scene.assetRegistry.registerAsset({ .uuid = someOtherUUID(), .type = AssetType::Prefab,
                                      .path = "Block", .body = oldPrefabBody });

  const nlohmann::json asset = {
    { "assetType", "prefab" },
    { "uuid", uuidString(someOtherUUID()) },
    { "name", "Block" },
    { "body", newPrefabBody }
  };

  const auto command = edits::commandForAddAsset(asset, scene.assetRegistry);
  ASSERT_TRUE(command.has_value());
  EXPECT_EQ(*command, edits::EditCommand::replaceAsset(someOtherUUID(), AssetType::Prefab, "Block", "",
                                                       oldPrefabBody, "Block", "", newPrefabBody));
}

// "Save as Prefab" mints a fresh uuid every time and the registry keys prefabs by name, so the incoming
// uuid is the one thing about the op that does not survive: the record already under that name is updated
// in place and keeps its own. Recording the incoming uuid would leave a command naming an asset that was
// never registered.
TEST(RecordEdits, AddAssetOverAnExistingPrefabNameReplacesTheRecordThatNameAlreadyHolds)
{
  AssetScene scene;
  SceneManager sceneManager;

  scene.assetRegistry.registerAsset({ .uuid = someOtherUUID(), .type = AssetType::Prefab,
                                      .path = "Block", .body = oldPrefabBody });

  const nlohmann::json asset = {
    { "assetType", "prefab" },
    { "uuid", uuidString(anotherUUID()) },
    { "name", "Block" },
    { "body", newPrefabBody }
  };

  const auto command = edits::commandForAddAsset(asset, scene.assetRegistry);
  ASSERT_TRUE(command.has_value());
  EXPECT_EQ(*command, edits::EditCommand::replaceAsset(someOtherUUID(), AssetType::Prefab, "Block", "",
                                                       oldPrefabBody, "Block", "", newPrefabBody));

  // What the registry actually does with that op, which is what the command has to describe.
  replication::applyAddAsset(scene.assetRegistry, sceneManager, scene.componentRegistry, asset);
  EXPECT_EQ(scene.assetRegistry.getByUUID(anotherUUID()), nullptr);
  ASSERT_NE(scene.assetRegistry.getByUUID(someOtherUUID()), nullptr);
  EXPECT_EQ(scene.assetRegistry.getByUUID(someOtherUUID())->body, newPrefabBody);
}

TEST(RecordEdits, AddAssetOverAnExistingPathOfAnyOtherTypeIsNotRecorded)
{
  AssetScene scene;
  SceneManager sceneManager;

  scene.assetRegistry.registerAsset({ .uuid = someOtherUUID(), .type = AssetType::Model,
                                      .path = "models/thing.obj" });

  const nlohmann::json reimport = {
    { "assetType", "model" },
    { "uuid", uuidString(anotherUUID()) },
    { "path", "models/thing.obj" }
  };

  EXPECT_FALSE(edits::commandForAddAsset(reimport, scene.assetRegistry).has_value());

  // registerAsset is first-wins for everything but a prefab body, so that op registers nothing at all -
  // there is no state change to record.
  replication::applyAddAsset(scene.assetRegistry, sceneManager, scene.componentRegistry, reimport);
  EXPECT_EQ(scene.assetRegistry.getByUUID(anotherUUID()), nullptr);

  // Positive control: the same import at a path nothing holds yet is recorded.
  const nlohmann::json fresh = {
    { "assetType", "model" },
    { "uuid", uuidString(anotherUUID()) },
    { "path", "models/other.obj" }
  };
  EXPECT_TRUE(edits::commandForAddAsset(fresh, scene.assetRegistry).has_value());
}

// --- addAsset's displayName field: carried on the wire, applied to a new record, and left alone by a
// prefab body update in place (see AGENTS.md's note on undo restoring the whole removed record).

TEST(RecordEdits, PackAndUnpackAddAssetRoundTripTheDisplayName)
{
  const nlohmann::json asset = {
    { "assetType", "model" },
    { "uuid", uuidString(someOtherUUID()) },
    { "path", "models/rock.obj" },
    { "displayName", "Rock" }
  };

  const auto unpacked = replication::unpackAddAsset(replication::packAddAsset(asset));
  EXPECT_EQ(unpacked.value("displayName", std::string{}), "Rock");
}

// A message packed before displayName existed carries only the original six strings; unpackAddAsset must
// still parse it (empty displayName) rather than underflow reading a seventh field that was never written.
TEST(RecordEdits, UnpackAddAssetToleratesAMessageWithNoDisplayNameField)
{
  net::Message legacy(net::MessageType::addAsset);
  legacy.writeString("model");
  legacy.writeString(uuidString(someOtherUUID()));
  legacy.writeString("models/rock.obj");
  legacy.writeString("");
  legacy.writeString("");
  legacy.writeString("");

  const auto unpacked = replication::unpackAddAsset(legacy);
  EXPECT_EQ(unpacked.value("displayName", std::string{}), "");
  EXPECT_EQ(unpacked.value("path", std::string{}), "models/rock.obj");

  AssetScene scene;
  SceneManager sceneManager;
  replication::applyAddAsset(scene.assetRegistry, sceneManager, scene.componentRegistry, unpacked);

  const auto* record = scene.assetRegistry.getByUUID(someOtherUUID());
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->path, "models/rock.obj");
  EXPECT_EQ(record->displayName, "");
}

TEST(RecordEdits, ApplyAddAssetWithADisplayNameRegistersTheRecordWithIt)
{
  AssetScene scene;
  SceneManager sceneManager;

  const nlohmann::json asset = {
    { "assetType", "model" },
    { "uuid", uuidString(someOtherUUID()) },
    { "path", "models/rock.obj" },
    { "displayName", "Rock" }
  };

  replication::applyAddAsset(scene.assetRegistry, sceneManager, scene.componentRegistry, asset);

  const auto* record = scene.assetRegistry.getByUUID(someOtherUUID());
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->displayName, "Rock");
}

TEST(RecordEdits, ApplyAddAssetOverAnExistingPrefabNameKeepsTheExistingDisplayName)
{
  AssetScene scene;
  SceneManager sceneManager;

  scene.assetRegistry.registerAsset({ .uuid = someOtherUUID(), .type = AssetType::Prefab,
                                      .path = "Block", .body = oldPrefabBody });
  scene.assetRegistry.renameAsset(someOtherUUID(), "Old Block");

  const nlohmann::json update = {
    { "assetType", "prefab" },
    { "uuid", uuidString(anotherUUID()) },
    { "name", "Block" },
    { "body", newPrefabBody }
  };

  replication::applyAddAsset(scene.assetRegistry, sceneManager, scene.componentRegistry, update);

  const auto* record = scene.assetRegistry.getByUUID(someOtherUUID());
  ASSERT_NE(record, nullptr);
  // Positive control: the body did change...
  EXPECT_EQ(record->body, newPrefabBody);
  // ...but the in-place update never touched the existing displayName override.
  EXPECT_EQ(record->displayName, "Old Block");
}

TEST(RecordEdits, RenameAndRemoveAssetCarryTheCurrentRecord)
{
  AssetScene scene;
  SceneManager sceneManager;

  scene.assetRegistry.registerAsset({ .uuid = someOtherUUID(), .type = AssetType::Script,
                                      .path = "scripts/Player.cs", .className = "PlayerScript" });
  scene.assetRegistry.renameAsset(someOtherUUID(), "Player");

  const auto renamed = edits::commandForRenameAsset(
    replication::buildRenameAsset(someOtherUUID(), "Hero"), scene.assetRegistry);
  ASSERT_TRUE(renamed.has_value());
  EXPECT_EQ(*renamed, edits::EditCommand::renameAsset(someOtherUUID(), "Player", "Hero"));

  const auto removed = edits::commandForRemoveAsset(replication::buildRemoveAsset(someOtherUUID()),
                                                    scene.assetRegistry, sceneManager);
  ASSERT_TRUE(removed.has_value());
  EXPECT_EQ(*removed, edits::EditCommand::removeAsset(someOtherUUID(), AssetType::Script,
                                                      "scripts/Player.cs", "PlayerScript", "", "Player"));

  // An asset the registry does not hold has no before state to read.
  EXPECT_FALSE(edits::commandForRenameAsset(replication::buildRenameAsset(anotherUUID(), "Hero"),
                                            scene.assetRegistry).has_value());
  EXPECT_FALSE(edits::commandForRemoveAsset(replication::buildRemoveAsset(anotherUUID()),
                                            scene.assetRegistry, sceneManager).has_value());
}

// --- replaceAsset through the history: the undo payload re-registers the old record, the redo payload
// the new one, and a body that moved underneath refuses both.

TEST(RecordEdits, ReplaceAssetUndoesToTheOldBodyAndRedoesToTheNewOne)
{
  AssetScene scene;
  SceneManager sceneManager;

  scene.assetRegistry.registerAsset({ .uuid = someOtherUUID(), .type = AssetType::Prefab,
                                      .path = "Block", .body = oldPrefabBody });

  const nlohmann::json asset = {
    { "assetType", "prefab" },
    { "uuid", uuidString(someOtherUUID()) },
    { "name", "Block" },
    { "body", newPrefabBody }
  };

  const auto command = edits::commandForAddAsset(asset, scene.assetRegistry);
  ASSERT_TRUE(command.has_value());

  replication::applyAddAsset(scene.assetRegistry, sceneManager, scene.componentRegistry, asset);
  ASSERT_EQ(scene.assetRegistry.getByUUID(someOtherUUID())->body, newPrefabBody);

  edits::EditHistory history;
  history.record(*command);

  const auto undoOutcome = history.undo(*scene.objectManager, &scene.assetRegistry);
  ASSERT_EQ(undoOutcome.result, edits::HistoryResult::applied);
  ASSERT_TRUE(undoOutcome.messagePayload.has_value());
  replication::applyAddAsset(scene.assetRegistry, sceneManager, scene.componentRegistry,
                             replication::unpackAddAsset(*undoOutcome.messagePayload));
  EXPECT_EQ(scene.assetRegistry.getByUUID(someOtherUUID())->body, oldPrefabBody);

  const auto redoOutcome = history.redo(*scene.objectManager, &scene.assetRegistry);
  ASSERT_EQ(redoOutcome.result, edits::HistoryResult::applied);
  ASSERT_TRUE(redoOutcome.messagePayload.has_value());
  replication::applyAddAsset(scene.assetRegistry, sceneManager, scene.componentRegistry,
                             replication::unpackAddAsset(*redoOutcome.messagePayload));
  EXPECT_EQ(scene.assetRegistry.getByUUID(someOtherUUID())->body, newPrefabBody);
}

TEST(RecordEdits, ReplaceAssetRefusesToUndoWhenTheBodyMovedUnderneath)
{
  AssetScene scene;
  SceneManager sceneManager;

  scene.assetRegistry.registerAsset({ .uuid = someOtherUUID(), .type = AssetType::Prefab,
                                      .path = "Block", .body = oldPrefabBody });

  const nlohmann::json asset = {
    { "assetType", "prefab" },
    { "uuid", uuidString(someOtherUUID()) },
    { "name", "Block" },
    { "body", newPrefabBody }
  };

  const auto command = edits::commandForAddAsset(asset, scene.assetRegistry);
  ASSERT_TRUE(command.has_value());
  replication::applyAddAsset(scene.assetRegistry, sceneManager, scene.componentRegistry, asset);

  edits::EditHistory history;
  history.record(*command);

  // Someone else saves over the same prefab before the undo runs.
  scene.assetRegistry.registerAsset({ .uuid = someOtherUUID(), .type = AssetType::Prefab,
                                      .path = "Block", .body = R"({"name":"Third"})" });

  const auto outcome = history.undo(*scene.objectManager, &scene.assetRegistry);
  EXPECT_EQ(outcome.result, edits::HistoryResult::targetChanged);
  ASSERT_TRUE(outcome.conflict.has_value());
  EXPECT_EQ(*outcome.conflict, someOtherUUID());
  EXPECT_FALSE(outcome.messagePayload.has_value());
}


// --- Asset-op refusals.

TEST(RecordEdits, AnAddAssetThatCannotBeFaithfullyDescribedIsNotRecorded)
{
  AssetScene scene;

  const nlohmann::json valid = {
    { "assetType", "model" },
    { "uuid", uuidString(someOtherUUID()) },
    { "path", "models/thing.obj" }
  };
  ASSERT_TRUE(edits::commandForAddAsset(valid, scene.assetRegistry).has_value());

  EXPECT_FALSE(edits::commandForAddAsset(nlohmann::json::array(), scene.assetRegistry).has_value());

  auto missingUUID = valid;
  missingUUID.erase("uuid");
  EXPECT_FALSE(edits::commandForAddAsset(missingUUID, scene.assetRegistry).has_value());

  auto malformedUUID = valid;
  malformedUUID["uuid"] = "not-a-uuid";
  EXPECT_FALSE(edits::commandForAddAsset(malformedUUID, scene.assetRegistry).has_value());

  auto numericUUID = valid;
  numericUUID["uuid"] = 12;
  EXPECT_FALSE(edits::commandForAddAsset(numericUUID, scene.assetRegistry).has_value());

  auto unknownType = valid;
  unknownType["assetType"] = "sound";
  EXPECT_FALSE(edits::commandForAddAsset(unknownType, scene.assetRegistry).has_value());

  auto missingType = valid;
  missingType.erase("assetType");
  EXPECT_FALSE(edits::commandForAddAsset(missingType, scene.assetRegistry).has_value());
}

// A prefab named after a path a file asset already holds is not a prefab update, so there is nothing to
// replace and nothing new gets registered.
TEST(RecordEdits, AnAddAssetOfAPrefabOverAFileAssetsPathIsNotRecorded)
{
  AssetScene scene;

  scene.assetRegistry.registerAsset({ .uuid = someOtherUUID(), .type = AssetType::Model, .path = "Block" });

  const nlohmann::json prefab = {
    { "assetType", "prefab" },
    { "uuid", uuidString(anotherUUID()) },
    { "name", "Block" },
    { "body", newPrefabBody }
  };
  EXPECT_FALSE(edits::commandForAddAsset(prefab, scene.assetRegistry).has_value());

  // Positive control: under a name nothing holds, the same prefab is an addition.
  auto fresh = prefab;
  fresh["name"] = "Fresh";
  EXPECT_TRUE(edits::commandForAddAsset(fresh, scene.assetRegistry).has_value());
}

TEST(RecordEdits, RenameAndRemoveAssetRefuseMalformedOps)
{
  AssetScene scene;
  SceneManager sceneManager;
  scene.assetRegistry.registerAsset({ .uuid = someOtherUUID(), .type = AssetType::Model,
                                      .path = "models/thing.obj" });

  const auto validRename = replication::buildRenameAsset(someOtherUUID(), "Thing");
  const auto validRemove = replication::buildRemoveAsset(someOtherUUID());
  ASSERT_TRUE(edits::commandForRenameAsset(validRename, scene.assetRegistry).has_value());
  ASSERT_TRUE(edits::commandForRemoveAsset(validRemove, scene.assetRegistry, sceneManager).has_value());

  EXPECT_FALSE(edits::commandForRenameAsset(nlohmann::json::array(), scene.assetRegistry).has_value());
  EXPECT_FALSE(edits::commandForRemoveAsset(nlohmann::json::array(), scene.assetRegistry,
                                            sceneManager).has_value());

  auto renameWithoutUUID = validRename;
  renameWithoutUUID.erase("uuid");
  auto removeWithoutUUID = validRemove;
  removeWithoutUUID.erase("uuid");
  EXPECT_FALSE(edits::commandForRenameAsset(renameWithoutUUID, scene.assetRegistry).has_value());
  EXPECT_FALSE(edits::commandForRemoveAsset(removeWithoutUUID, scene.assetRegistry, sceneManager).has_value());

  auto renameWithBadUUID = validRename;
  renameWithBadUUID["uuid"] = "not-a-uuid";
  auto removeWithBadUUID = validRemove;
  removeWithBadUUID["uuid"] = "not-a-uuid";
  EXPECT_FALSE(edits::commandForRenameAsset(renameWithBadUUID, scene.assetRegistry).has_value());
  EXPECT_FALSE(edits::commandForRemoveAsset(removeWithBadUUID, scene.assetRegistry, sceneManager).has_value());
}
