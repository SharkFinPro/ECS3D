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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <uuid.h>
#include <vector>

namespace {
  // Every scene-edit test derives a command from a view that already holds one root object.
  struct Scene : fixtures::Scene {
    std::shared_ptr<Object> object;
  };

  Scene makeScene()
  {
    Scene scene;
    scene.object = addObject(scene, "Object");
    return scene;
  }

  // Asset-kind tests need a registry; the ObjectManager comes along because EditHistory::undo/redo take
  // one unconditionally (unused by asset-kind commands).
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

  std::string uuidString(const uuids::uuid& uuid)
  {
    return uuids::to_string(uuid);
  }

  std::shared_ptr<Component> scriptNamed(const std::shared_ptr<Object>& object, const std::string& className)
  {
    for (const auto& script : object->getScripts())
    {
      if (const auto component = std::dynamic_pointer_cast<Script>(script);
          component && component->getClassName() == className)
      {
        return script;
      }
    }

    return nullptr;
  }

  // Undo of a structural edit can rebuild objects, so sibling lists are compared by uuid, not by pointer.
  std::vector<uuids::uuid> uuidsOf(const std::vector<std::shared_ptr<Object>>& objects)
  {
    std::vector<uuids::uuid> result;
    for (const auto& object : objects)
    {
      result.push_back(object->getUUID());
    }

    return result;
  }

  void applyPayload(const Scene& scene, const std::optional<nlohmann::json>& payload)
  {
    ASSERT_TRUE(payload.has_value());
    ASSERT_EQ(replication::applySceneEdit(*scene.objectManager, *payload),
              replication::SceneEditResult::applied);
  }

  const std::string oldPrefabBody = R"({"name":"Old"})";
  const std::string newPrefabBody = R"({"name":"New"})";
}

// --- Object creation: the created uuid is only recorded when the op happens to carry one, and the
// sibling index is always the appended position.

TEST(RecordEdits, AddObjectAtTheRootRecordsAnAppendedIndex)
{
  const auto scene = makeScene();

  const auto command = edits::commandForSceneEdit(replication::buildAddObject("Thing"),
                                                  *scene.objectManager);

  ASSERT_TRUE(command.has_value());
  EXPECT_EQ(*command, edits::EditCommand::addObject(uuids::uuid{}, std::nullopt, "Thing", 1));
}

TEST(RecordEdits, AddObjectUnderAParentCountsThatParentsChildren)
{
  const auto scene = makeScene();
  addChildObject(scene, "First", scene.object);
  addChildObject(scene, "Second", scene.object);

  const auto parentUUID = scene.object->getUUID();
  const auto command = edits::commandForSceneEdit(replication::buildAddObject("Thing", &parentUUID),
                                                  *scene.objectManager);

  ASSERT_TRUE(command.has_value());
  EXPECT_EQ(*command, edits::EditCommand::addObject(uuids::uuid{}, parentUUID, "Thing", 2));
}

TEST(RecordEdits, AddObjectTakesTheCreatedUUIDWhenTheOpCarriesOne)
{
  const auto scene = makeScene();

  nlohmann::json edit = replication::buildAddObject("Thing");
  edit["uuid"] = uuidString(someOtherUUID());

  const auto command = edits::commandForSceneEdit(edit, *scene.objectManager);

  ASSERT_TRUE(command.has_value());
  EXPECT_EQ(*command, edits::EditCommand::addObject(someOtherUUID(), std::nullopt, "Thing", 1));
}

TEST(RecordEdits, AddObjectUnderAParentTheViewDoesNotHaveIsNotRecorded)
{
  const auto scene = makeScene();

  const auto missingParent = someOtherUUID();
  EXPECT_FALSE(edits::commandForSceneEdit(replication::buildAddObject("Thing", &missingParent),
                                          *scene.objectManager).has_value());

  // Positive control: the same op under a parent the view does have is recorded.
  const auto parentUUID = scene.object->getUUID();
  EXPECT_TRUE(edits::commandForSceneEdit(replication::buildAddObject("Thing", &parentUUID),
                                         *scene.objectManager).has_value());
}

// --- Removal: the before state is the object's place in its sibling list plus its whole subtree.

TEST(RecordEdits, RemoveObjectRecordsTheRootSiblingIndexAndSubtree)
{
  auto scene = makeScene();
  const auto second = addObject(scene, "Second");

  const auto command = edits::commandForSceneEdit(replication::buildRemoveObject(second->getUUID()),
                                                  *scene.objectManager);

  ASSERT_TRUE(command.has_value());
  EXPECT_EQ(*command, edits::EditCommand::removeObject(second->getUUID(), std::nullopt, 1,
                                                       second->serialize()));
}

TEST(RecordEdits, RemoveObjectOfAChildRecordsItsIndexWithinTheParent)
{
  const auto scene = makeScene();
  addChildObject(scene, "First", scene.object);
  const auto second = addChildObject(scene, "Second", scene.object);

  const auto command = edits::commandForSceneEdit(replication::buildRemoveObject(second->getUUID()),
                                                  *scene.objectManager);

  ASSERT_TRUE(command.has_value());
  EXPECT_EQ(*command, edits::EditCommand::removeObject(second->getUUID(), scene.object->getUUID(), 1,
                                                       second->serialize()));
}

// --- Reparent / rename: the before state comes from the view, the after state from the op.

TEST(RecordEdits, ReparentObjectRecordsTheCurrentParentAsTheBeforeState)
{
  const auto scene = makeScene();
  const auto child = addChildObject(scene, "Child", scene.object);

  // Onto the root: the op names no parent at all.
  const auto toRoot = edits::commandForSceneEdit(replication::buildReparentObject(child->getUUID()),
                                                 *scene.objectManager);
  ASSERT_TRUE(toRoot.has_value());
  EXPECT_EQ(*toRoot, edits::EditCommand::reparentObject(child->getUUID(), scene.object->getUUID(),
                                                        std::nullopt));

  // And the other direction, from a root object onto a parent.
  const auto other = addObject(scene, "Other");
  const auto parentUUID = scene.object->getUUID();
  const auto toParent = edits::commandForSceneEdit(
    replication::buildReparentObject(other->getUUID(), &parentUUID), *scene.objectManager);
  ASSERT_TRUE(toParent.has_value());
  EXPECT_EQ(*toParent, edits::EditCommand::reparentObject(other->getUUID(), std::nullopt, parentUUID));
}

TEST(RecordEdits, RenameObjectRecordsTheNameTheViewStillHas)
{
  const auto scene = makeScene();

  const auto command = edits::commandForSceneEdit(
    replication::buildRenameObject(scene.object->getUUID(), "Renamed"), *scene.objectManager);

  ASSERT_TRUE(command.has_value());
  EXPECT_EQ(*command, edits::EditCommand::renameObject(scene.object->getUUID(), "Object", "Renamed"));
}

// --- Components: an added script is the addComponent kind under the "Script" key, and a removal has to
// name a component the object is actually carrying.

TEST(RecordEdits, AddComponentAndAddScriptBothRecordAnAddComponent)
{
  const auto scene = makeScene();

  const auto component = edits::commandForSceneEdit(
    replication::buildAddComponent(scene.object->getUUID(), "RigidBody"), *scene.objectManager);
  ASSERT_TRUE(component.has_value());
  EXPECT_EQ(*component, edits::EditCommand::addComponent(scene.object->getUUID(), "RigidBody"));

  const auto script = edits::commandForSceneEdit(
    replication::buildAddScript(scene.object->getUUID(), "PlayerScript"), *scene.objectManager);
  ASSERT_TRUE(script.has_value());
  EXPECT_EQ(*script, edits::EditCommand::addComponent(scene.object->getUUID(), "Script", "PlayerScript"));
}

TEST(RecordEdits, RemoveComponentPicksTheComponentWithTheMatchingType)
{
  const auto scene = makeScene();

  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
              replication::buildAddComponent(scene.object->getUUID(), "RigidBody")),
            replication::SceneEditResult::applied);
  const auto rigidBody = scene.object->getComponents().at(ComponentType::rigidBody);

  const nlohmann::json edit = {
    { "op", "removeComponent" },
    { "object", uuidString(scene.object->getUUID()) },
    { "type", "RigidBody" }
  };

  const auto command = edits::commandForSceneEdit(edit, *scene.objectManager);
  ASSERT_TRUE(command.has_value());
  EXPECT_EQ(*command, edits::EditCommand::removeComponent(scene.object->getUUID(),
                                                          rigidBody->serialize()));

  // The Transform the object does carry is still recorded, so the refusal below is about the named type
  // and not about this op shape.
  const nlohmann::json transformEdit = {
    { "op", "removeComponent" },
    { "object", uuidString(scene.object->getUUID()) },
    { "type", "Transform" }
  };
  EXPECT_TRUE(edits::commandForSceneEdit(transformEdit, *scene.objectManager).has_value());

  const nlohmann::json missingEdit = {
    { "op", "removeComponent" },
    { "object", uuidString(scene.object->getUUID()) },
    { "type", "Camera" }
  };
  EXPECT_FALSE(edits::commandForSceneEdit(missingEdit, *scene.objectManager).has_value());
}

TEST(RecordEdits, RemoveComponentPicksAScriptByClassNameWhenSeveralAreAttached)
{
  const auto scene = makeScene();

  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
              replication::buildAddScript(scene.object->getUUID(), "FirstScript")),
            replication::SceneEditResult::applied);
  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager,
              replication::buildAddScript(scene.object->getUUID(), "SecondScript")),
            replication::SceneEditResult::applied);
  ASSERT_EQ(scene.object->getScripts().size(), 2u);

  const auto second = scriptNamed(scene.object, "SecondScript");
  ASSERT_NE(second, nullptr);

  const nlohmann::json edit = {
    { "op", "removeComponent" },
    { "object", uuidString(scene.object->getUUID()) },
    { "type", "Script" },
    { "className", "SecondScript" }
  };

  const auto command = edits::commandForSceneEdit(edit, *scene.objectManager);
  ASSERT_TRUE(command.has_value());
  EXPECT_EQ(*command, edits::EditCommand::removeComponent(scene.object->getUUID(), second->serialize()));

  const nlohmann::json missingEdit = {
    { "op", "removeComponent" },
    { "object", uuidString(scene.object->getUUID()) },
    { "type", "Script" },
    { "className", "ThirdScript" }
  };
  EXPECT_FALSE(edits::commandForSceneEdit(missingEdit, *scene.objectManager).has_value());
}

// --- The creating ops that copy a subtree.

TEST(RecordEdits, DuplicateObjectRecordsTheSourcesParentAndAnAppendedIndex)
{
  const auto scene = makeScene();
  const auto child = addChildObject(scene, "Child", scene.object);

  const auto rootDuplicate = edits::commandForSceneEdit(
    replication::buildDuplicateObject(scene.object->getUUID()), *scene.objectManager);
  ASSERT_TRUE(rootDuplicate.has_value());
  EXPECT_EQ(*rootDuplicate, edits::EditCommand::duplicateObject(scene.object->getUUID(), uuids::uuid{},
                                                                std::nullopt, 1));

  nlohmann::json childEdit = replication::buildDuplicateObject(child->getUUID());
  childEdit["uuid"] = uuidString(someOtherUUID());

  const auto childDuplicate = edits::commandForSceneEdit(childEdit, *scene.objectManager);
  ASSERT_TRUE(childDuplicate.has_value());
  EXPECT_EQ(*childDuplicate, edits::EditCommand::duplicateObject(child->getUUID(), someOtherUUID(),
                                                                 scene.object->getUUID(), 1));
}

TEST(RecordEdits, InstantiatePrefabRecordsThePrefabParentAppendedIndexAndCurrentBody)
{
  AssetScene scene;
  const auto object = addObject(scene, "Object");

  const auto prefabUUID = someOtherUUID();
  const std::string body = R"({"name":"Prefab"})";
  scene.assetRegistry.registerAsset({ .uuid = prefabUUID, .type = AssetType::Prefab, .path = "Prefab",
                                      .body = body });

  const auto atRoot = edits::commandForSceneEdit(replication::buildInstantiatePrefab(prefabUUID),
                                                 *scene.objectManager, &scene.assetRegistry);
  ASSERT_TRUE(atRoot.has_value());
  EXPECT_EQ(*atRoot,
            edits::EditCommand::instantiatePrefab(prefabUUID, uuids::uuid{}, std::nullopt, 1, body));

  const auto parentUUID = object->getUUID();
  nlohmann::json underParent = replication::buildInstantiatePrefab(prefabUUID, &parentUUID);
  underParent["uuid"] = uuidString(anotherUUID());

  const auto command = edits::commandForSceneEdit(underParent, *scene.objectManager, &scene.assetRegistry);
  ASSERT_TRUE(command.has_value());
  EXPECT_EQ(*command,
            edits::EditCommand::instantiatePrefab(prefabUUID, anotherUUID(), parentUUID, 0, body));
}

TEST(RecordEdits, InstantiatePrefabIsNotRecordedWithoutTheAssetRegistry)
{
  const auto scene = makeScene();

  // Positive control lives in InstantiatePrefabRecordsThePrefabParentAppendedIndexAndCurrentBody above -
  // the same op derives a command once an AssetRegistry holding the prefab is passed. Without one, there
  // is no way to capture the body redo needs to validate against, so nothing faithful can be recorded.
  EXPECT_FALSE(edits::commandForSceneEdit(replication::buildInstantiatePrefab(someOtherUUID()),
                                          *scene.objectManager).has_value());
}

// --- Nothing faithful to record.

TEST(RecordEdits, AnOpNamingAnObjectTheViewDoesNotHaveIsNotRecorded)
{
  const auto scene = makeScene();

  EXPECT_FALSE(edits::commandForSceneEdit(replication::buildRenameObject(someOtherUUID(), "Renamed"),
                                          *scene.objectManager).has_value());

  // Positive control: the same op against an object the view does have.
  EXPECT_TRUE(edits::commandForSceneEdit(
    replication::buildRenameObject(scene.object->getUUID(), "Renamed"), *scene.objectManager).has_value());
}

TEST(RecordEdits, AnUnknownOpIsNotRecorded)
{
  const auto scene = makeScene();

  const nlohmann::json edit = {
    { "op", "explodeObject" },
    { "object", uuidString(scene.object->getUUID()) }
  };

  EXPECT_FALSE(edits::commandForSceneEdit(edit, *scene.objectManager).has_value());

  // Positive control: the same object under an op that does have a command kind.
  EXPECT_TRUE(edits::commandForSceneEdit(replication::buildDuplicateObject(scene.object->getUUID()),
                                         *scene.objectManager).has_value());
}

TEST(RecordEdits, AMalformedUUIDIsNotRecorded)
{
  const auto scene = makeScene();

  const nlohmann::json edit = {
    { "op", "removeObject" },
    { "object", "not-a-uuid" }
  };
  EXPECT_FALSE(edits::commandForSceneEdit(edit, *scene.objectManager).has_value());

  // Positive control: the same op with the uuid spelled properly.
  EXPECT_TRUE(edits::commandForSceneEdit(replication::buildRemoveObject(scene.object->getUUID()),
                                         *scene.objectManager).has_value());
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

  scene.assetRegistry.registerAsset({ .uuid = someOtherUUID(), .type = AssetType::Script,
                                      .path = "scripts/Player.cs", .className = "PlayerScript" });
  scene.assetRegistry.renameAsset(someOtherUUID(), "Player");

  const auto renamed = edits::commandForRenameAsset(
    replication::buildRenameAsset(someOtherUUID(), "Hero"), scene.assetRegistry);
  ASSERT_TRUE(renamed.has_value());
  EXPECT_EQ(*renamed, edits::EditCommand::renameAsset(someOtherUUID(), "Player", "Hero"));

  const auto removed = edits::commandForRemoveAsset(replication::buildRemoveAsset(someOtherUUID()),
                                                    scene.assetRegistry);
  ASSERT_TRUE(removed.has_value());
  EXPECT_EQ(*removed, edits::EditCommand::removeAsset(someOtherUUID(), AssetType::Script,
                                                      "scripts/Player.cs", "PlayerScript", "", "Player"));

  // An asset the registry does not hold has no before state to read.
  EXPECT_FALSE(edits::commandForRenameAsset(replication::buildRenameAsset(anotherUUID(), "Hero"),
                                            scene.assetRegistry).has_value());
  EXPECT_FALSE(edits::commandForRemoveAsset(replication::buildRemoveAsset(anotherUUID()),
                                            scene.assetRegistry).has_value());
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

// --- reorderObject: the command is derived from the pre-edit view, and undoing and redoing it lands the
// object back on the sibling slots the view read.

TEST(RecordEdits, ASameParentReorderRecordsBothSlotsAndUndoesAndRedoesThem)
{
  const auto scene = makeScene();
  const auto a = addChildObject(scene, "A", scene.object);
  const auto b = addChildObject(scene, "B", scene.object);
  const auto c = addChildObject(scene, "C", scene.object);
  const auto parentUUID = scene.object->getUUID();

  const auto edit = replication::buildReorderObject(a->getUUID(), &parentUUID, 2);
  const auto command = edits::commandForSceneEdit(edit, *scene.objectManager);

  ASSERT_TRUE(command.has_value());
  EXPECT_EQ(*command, edits::EditCommand::reorderObject(a->getUUID(), parentUUID, 0, parentUUID, 2));

  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager, edit), replication::SceneEditResult::applied);
  ASSERT_EQ(uuidsOf(scene.object->getChildren()),
            (std::vector<uuids::uuid>{ b->getUUID(), c->getUUID(), a->getUUID() }));

  edits::EditHistory history;
  history.record(*command);

  const auto undoOutcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_NO_FATAL_FAILURE(applyPayload(scene, undoOutcome.jsonPayload));
  EXPECT_EQ(uuidsOf(scene.object->getChildren()),
            (std::vector<uuids::uuid>{ a->getUUID(), b->getUUID(), c->getUUID() }));

  const auto redoOutcome = history.redo(*scene.objectManager);
  ASSERT_TRUE(redoOutcome.ok());
  ASSERT_NO_FATAL_FAILURE(applyPayload(scene, redoOutcome.jsonPayload));
  EXPECT_EQ(uuidsOf(scene.object->getChildren()),
            (std::vector<uuids::uuid>{ b->getUUID(), c->getUUID(), a->getUUID() }));
}

TEST(RecordEdits, AReorderToAnotherParentRecordsTheSourceSlotAndUndoesAndRedoesIt)
{
  const auto scene = makeScene();
  const auto source = addObject(scene, "Source");
  const auto dest = addObject(scene, "Dest");
  const auto first = addChildObject(scene, "First", source);
  const auto moved = addChildObject(scene, "Moved", source);
  const auto destA = addChildObject(scene, "DestA", dest);
  const auto destB = addChildObject(scene, "DestB", dest);
  const auto destUUID = dest->getUUID();

  const auto edit = replication::buildReorderObject(moved->getUUID(), &destUUID, 1);
  const auto command = edits::commandForSceneEdit(edit, *scene.objectManager);

  ASSERT_TRUE(command.has_value());
  EXPECT_EQ(*command, edits::EditCommand::reorderObject(moved->getUUID(), source->getUUID(), 1,
                                                        destUUID, 1));

  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager, edit), replication::SceneEditResult::applied);
  ASSERT_EQ(uuidsOf(dest->getChildren()),
            (std::vector<uuids::uuid>{ destA->getUUID(), moved->getUUID(), destB->getUUID() }));

  edits::EditHistory history;
  history.record(*command);

  const auto undoOutcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_NO_FATAL_FAILURE(applyPayload(scene, undoOutcome.jsonPayload));
  EXPECT_EQ(uuidsOf(source->getChildren()), (std::vector<uuids::uuid>{ first->getUUID(), moved->getUUID() }));
  EXPECT_EQ(uuidsOf(dest->getChildren()), (std::vector<uuids::uuid>{ destA->getUUID(), destB->getUUID() }));

  const auto redoOutcome = history.redo(*scene.objectManager);
  ASSERT_TRUE(redoOutcome.ok());
  ASSERT_NO_FATAL_FAILURE(applyPayload(scene, redoOutcome.jsonPayload));
  EXPECT_EQ(uuidsOf(source->getChildren()), (std::vector<uuids::uuid>{ first->getUUID() }));
  EXPECT_EQ(uuidsOf(dest->getChildren()),
            (std::vector<uuids::uuid>{ destA->getUUID(), moved->getUUID(), destB->getUUID() }));
}

TEST(RecordEdits, AReorderOntoTheSceneRootNamesNoAfterParentAndUndoesBackUnderTheOldOne)
{
  const auto scene = makeScene();
  const auto child = addChildObject(scene, "Child", scene.object);

  const auto edit = replication::buildReorderObject(child->getUUID(), nullptr, 0);
  const auto command = edits::commandForSceneEdit(edit, *scene.objectManager);

  ASSERT_TRUE(command.has_value());
  EXPECT_EQ(*command, edits::EditCommand::reorderObject(child->getUUID(), scene.object->getUUID(), 0,
                                                        std::nullopt, 0));

  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager, edit), replication::SceneEditResult::applied);
  ASSERT_EQ(uuidsOf(scene.objectManager->getObjects()),
            (std::vector<uuids::uuid>{ child->getUUID(), scene.object->getUUID() }));

  edits::EditHistory history;
  history.record(*command);

  const auto undoOutcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_NO_FATAL_FAILURE(applyPayload(scene, undoOutcome.jsonPayload));
  EXPECT_EQ(uuidsOf(scene.objectManager->getObjects()), (std::vector<uuids::uuid>{ scene.object->getUUID() }));
  EXPECT_EQ(uuidsOf(scene.object->getChildren()), (std::vector<uuids::uuid>{ child->getUUID() }));

  const auto redoOutcome = history.redo(*scene.objectManager);
  ASSERT_TRUE(redoOutcome.ok());
  ASSERT_NO_FATAL_FAILURE(applyPayload(scene, redoOutcome.jsonPayload));
  EXPECT_EQ(uuidsOf(scene.objectManager->getObjects()),
            (std::vector<uuids::uuid>{ child->getUUID(), scene.object->getUUID() }));
}

TEST(RecordEdits, AMalformedReorderIsNotRecorded)
{
  const auto scene = makeScene();
  const auto child = addChildObject(scene, "Child", scene.object);
  const auto parentUUID = scene.object->getUUID();

  const auto valid = replication::buildReorderObject(child->getUUID(), &parentUUID, 0);
  ASSERT_TRUE(edits::commandForSceneEdit(valid, *scene.objectManager).has_value());

  auto badParent = valid;
  badParent["parent"] = "not-a-uuid";
  EXPECT_FALSE(edits::commandForSceneEdit(badParent, *scene.objectManager).has_value());

  auto nonStringParent = valid;
  nonStringParent["parent"] = 7;
  EXPECT_FALSE(edits::commandForSceneEdit(nonStringParent, *scene.objectManager).has_value());

  auto missingIndex = valid;
  missingIndex.erase("index");
  EXPECT_FALSE(edits::commandForSceneEdit(missingIndex, *scene.objectManager).has_value());

  auto negativeIndex = valid;
  negativeIndex["index"] = -1;
  EXPECT_FALSE(edits::commandForSceneEdit(negativeIndex, *scene.objectManager).has_value());

  auto fractionalIndex = valid;
  fractionalIndex["index"] = 1.5;
  EXPECT_FALSE(edits::commandForSceneEdit(fractionalIndex, *scene.objectManager).has_value());

  auto stringIndex = valid;
  stringIndex["index"] = "0";
  EXPECT_FALSE(edits::commandForSceneEdit(stringIndex, *scene.objectManager).has_value());

  const auto unknownObject = replication::buildReorderObject(someOtherUUID(), &parentUUID, 0);
  EXPECT_FALSE(edits::commandForSceneEdit(unknownObject, *scene.objectManager).has_value());
}

// An object a script spawned mid-pass is findable by uuid but is not in the root list yet, so the view
// cannot say where it sits; the recorded index falls back to the end of the list.
TEST(RecordEdits, ARemovalOfAnObjectNotYetInTheRootListRecordsTheEndOfTheList)
{
  const auto scene = makeScene();

  const ObjectManager::ScriptPassGuard guard(*scene.objectManager);
  const auto spawned = std::make_shared<Object>("Spawned");
  scene.objectManager->addObject(spawned);
  ASSERT_EQ(scene.objectManager->getObjects().size(), 1u);
  ASSERT_EQ(scene.objectManager->getObjectByUUID(spawned->getUUID()), spawned);

  const auto command = edits::commandForSceneEdit(replication::buildRemoveObject(spawned->getUUID()),
                                                  *scene.objectManager);

  ASSERT_TRUE(command.has_value());
  EXPECT_EQ(*command, edits::EditCommand::removeObject(spawned->getUUID(), std::nullopt, 1,
                                                       spawned->serialize()));

  // Positive control: an object that is in the list records its real index.
  const auto listed = edits::commandForSceneEdit(replication::buildRemoveObject(scene.object->getUUID()),
                                                 *scene.objectManager);
  ASSERT_TRUE(listed.has_value());
  EXPECT_EQ(*listed, edits::EditCommand::removeObject(scene.object->getUUID(), std::nullopt, 0,
                                                      scene.object->serialize()));
}

// --- Scene-edit refusals that are not about one op's own fields.

TEST(RecordEdits, ANonObjectEditIsNotRecorded)
{
  const auto scene = makeScene();

  EXPECT_FALSE(edits::commandForSceneEdit(nlohmann::json::array(), *scene.objectManager).has_value());
  EXPECT_FALSE(edits::commandsForSceneEdit(nlohmann::json::array(), *scene.objectManager).has_value());
  EXPECT_FALSE(edits::commandsForSceneEdit(nlohmann::json("batch"), *scene.objectManager).has_value());

  // Positive control: an object-shaped edit goes through both entry points.
  const auto edit = replication::buildRemoveObject(scene.object->getUUID());
  EXPECT_TRUE(edits::commandForSceneEdit(edit, *scene.objectManager).has_value());
  EXPECT_TRUE(edits::commandsForSceneEdit(edit, *scene.objectManager).has_value());
}

TEST(RecordEdits, OpsMissingTheFieldTheirCommandNeedsAreNotRecorded)
{
  const auto scene = makeScene();
  const auto objectUUID = uuidString(scene.object->getUUID());

  const nlohmann::json renameWithoutName = { { "op", "renameObject" }, { "object", objectUUID } };
  const nlohmann::json renameWithNumber = { { "op", "renameObject" }, { "object", objectUUID },
                                            { "name", 4 } };
  const nlohmann::json addWithoutKey = { { "op", "addComponent" }, { "object", objectUUID } };
  const nlohmann::json removeWithoutType = { { "op", "removeComponent" }, { "object", objectUUID } };
  const nlohmann::json instantiateWithoutPrefab = { { "op", "instantiatePrefab" } };
  const nlohmann::json reparentWithBadParent = { { "op", "reparentObject" }, { "object", objectUUID },
                                                 { "parent", "not-a-uuid" } };

  EXPECT_FALSE(edits::commandForSceneEdit(renameWithoutName, *scene.objectManager).has_value());
  EXPECT_FALSE(edits::commandForSceneEdit(renameWithNumber, *scene.objectManager).has_value());
  EXPECT_FALSE(edits::commandForSceneEdit(addWithoutKey, *scene.objectManager).has_value());
  EXPECT_FALSE(edits::commandForSceneEdit(removeWithoutType, *scene.objectManager).has_value());
  EXPECT_FALSE(edits::commandForSceneEdit(instantiateWithoutPrefab, *scene.objectManager).has_value());
  EXPECT_FALSE(edits::commandForSceneEdit(reparentWithBadParent, *scene.objectManager).has_value());

  // Positive control: the same ops with their field present are recorded.
  EXPECT_TRUE(edits::commandForSceneEdit(replication::buildRenameObject(scene.object->getUUID(), "Named"),
                                         *scene.objectManager).has_value());
  EXPECT_TRUE(edits::commandForSceneEdit(replication::buildAddComponent(scene.object->getUUID(), "RigidBody"),
                                         *scene.objectManager).has_value());
}

TEST(RecordEdits, ASingleNonBatchOpYieldsAOneElementVectorEqualToItsOwnCommand)
{
  const auto scene = makeScene();

  const auto edit = replication::buildRenameObject(scene.object->getUUID(), "Renamed");
  const auto single = edits::commandForSceneEdit(edit, *scene.objectManager);
  ASSERT_TRUE(single.has_value());

  const auto commands = edits::commandsForSceneEdit(edit, *scene.objectManager);
  ASSERT_TRUE(commands.has_value());
  ASSERT_EQ(commands->size(), 1u);
  EXPECT_EQ(commands->front(), *single);

  // A non-batch op with nothing to record gives nothing, not an empty vector.
  EXPECT_FALSE(edits::commandsForSceneEdit(replication::buildRenameObject(someOtherUUID(), "Renamed"),
                                           *scene.objectManager).has_value());
}

namespace
{
  nlohmann::json signedReorder(const std::shared_ptr<Object>& object, const int index)
  {
    return {
      { "op", "reorderObject" },
      { "object", uuids::to_string(object->getUUID()) },
      { "index", index }
    };
  }
}

TEST(RecordEdits, AReorderWithASignedIndexIsRecordedLikeAnUnsignedOne)
{
  const auto scene = makeScene();
  const auto a = addChildObject(scene, "A", scene.object);
  addChildObject(scene, "B", scene.object);
  const auto parentUUID = scene.object->getUUID();

  const auto edit = signedReorder(a, 1);
  ASSERT_TRUE(edit.at("index").is_number_integer());
  ASSERT_FALSE(edit.at("index").is_number_unsigned());

  const auto command = edits::commandForSceneEdit(edit, *scene.objectManager);
  ASSERT_TRUE(command.has_value());
  EXPECT_EQ(*command, edits::EditCommand::reorderObject(a->getUUID(), parentUUID, 0, std::nullopt, 1));
}

TEST(RecordEdits, ANegativeSignedReorderIndexIsRefusedByTheRecorderAndTheApplier)
{
  const auto scene = makeScene();
  const auto a = addChildObject(scene, "A", scene.object);

  EXPECT_TRUE(edits::commandForSceneEdit(signedReorder(a, 0), *scene.objectManager).has_value());

  const auto negative = signedReorder(a, -1);
  EXPECT_FALSE(edits::commandForSceneEdit(negative, *scene.objectManager).has_value());
  EXPECT_NE(replication::applySceneEdit(*scene.objectManager, negative), replication::SceneEditResult::applied);
}

TEST(RecordEdits, ABatchWithASignedIndexReorderIsRecorded)
{
  const auto scene = makeScene();
  const auto a = addChildObject(scene, "A", scene.object);
  addChildObject(scene, "B", scene.object);

  const auto batch = replication::buildBatch({ signedReorder(a, 1) });
  const auto recorded = edits::commandsForSceneEdit(batch, *scene.objectManager);
  ASSERT_TRUE(recorded.has_value());
  EXPECT_EQ(recorded->size(), 1u);
}

TEST(RecordEdits, ParseIndexFieldAcceptsNonNegativeIntegersOnly)
{
  EXPECT_EQ(replication::parseIndexField(nlohmann::json(std::uint64_t{ 3 })), std::optional<std::size_t>(3));
  EXPECT_EQ(replication::parseIndexField(nlohmann::json(3)), std::optional<std::size_t>(3));
  EXPECT_EQ(replication::parseIndexField(nlohmann::json(0)), std::optional<std::size_t>(0));
  EXPECT_FALSE(replication::parseIndexField(nlohmann::json(-1)).has_value());
  EXPECT_FALSE(replication::parseIndexField(nlohmann::json(1.5)).has_value());
  EXPECT_FALSE(replication::parseIndexField(nlohmann::json("1")).has_value());
  EXPECT_FALSE(replication::parseIndexField(nlohmann::json()).has_value());
}

TEST(RecordEdits, ABatchThatCannotBeFullyRecordedIsNotRecorded)
{
  const auto scene = makeScene();
  const auto child = addChildObject(scene, "Child", scene.object);
  const auto parentUUID = scene.object->getUUID();

  const auto rename = replication::buildRenameObject(scene.object->getUUID(), "Renamed");
  const auto addObjectOp = replication::buildAddObject("New");

  const auto valid = replication::buildBatch({ rename, addObjectOp });
  const auto recorded = edits::commandsForSceneEdit(valid, *scene.objectManager);
  ASSERT_TRUE(recorded.has_value());
  EXPECT_EQ(recorded->size(), 2u);

  nlohmann::json withoutOps = valid;
  withoutOps.erase("ops");
  EXPECT_FALSE(edits::commandsForSceneEdit(withoutOps, *scene.objectManager).has_value());

  nlohmann::json opsNotAnArray = valid;
  opsNotAnArray["ops"] = "rename";
  EXPECT_FALSE(edits::commandsForSceneEdit(opsNotAnArray, *scene.objectManager).has_value());

  EXPECT_FALSE(edits::commandsForSceneEdit(replication::buildBatch({}), *scene.objectManager).has_value());

  // One op the view cannot derive a command for.
  const auto unknownTarget = replication::buildBatch(
    { rename, replication::buildRenameObject(someOtherUUID(), "Ghost") });
  EXPECT_FALSE(edits::commandsForSceneEdit(unknownTarget, *scene.objectManager).has_value());

  // One op that records but that the authority would refuse: index 5 is past the end of the list.
  const auto pastTheEnd = replication::buildReorderObject(child->getUUID(), &parentUUID, 5);
  ASSERT_TRUE(edits::commandForSceneEdit(pastTheEnd, *scene.objectManager).has_value());
  EXPECT_FALSE(edits::commandsForSceneEdit(replication::buildBatch({ rename, pastTheEnd }),
                                           *scene.objectManager).has_value());

  // A batch inside a batch is not an op either.
  EXPECT_FALSE(edits::commandsForSceneEdit(replication::buildBatch({ rename, valid }),
                                           *scene.objectManager).has_value());
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
  scene.assetRegistry.registerAsset({ .uuid = someOtherUUID(), .type = AssetType::Model,
                                      .path = "models/thing.obj" });

  const auto validRename = replication::buildRenameAsset(someOtherUUID(), "Thing");
  const auto validRemove = replication::buildRemoveAsset(someOtherUUID());
  ASSERT_TRUE(edits::commandForRenameAsset(validRename, scene.assetRegistry).has_value());
  ASSERT_TRUE(edits::commandForRemoveAsset(validRemove, scene.assetRegistry).has_value());

  EXPECT_FALSE(edits::commandForRenameAsset(nlohmann::json::array(), scene.assetRegistry).has_value());
  EXPECT_FALSE(edits::commandForRemoveAsset(nlohmann::json::array(), scene.assetRegistry).has_value());

  auto renameWithoutUUID = validRename;
  renameWithoutUUID.erase("uuid");
  auto removeWithoutUUID = validRemove;
  removeWithoutUUID.erase("uuid");
  EXPECT_FALSE(edits::commandForRenameAsset(renameWithoutUUID, scene.assetRegistry).has_value());
  EXPECT_FALSE(edits::commandForRemoveAsset(removeWithoutUUID, scene.assetRegistry).has_value());

  auto renameWithBadUUID = validRename;
  renameWithBadUUID["uuid"] = "not-a-uuid";
  auto removeWithBadUUID = validRemove;
  removeWithBadUUID["uuid"] = "not-a-uuid";
  EXPECT_FALSE(edits::commandForRenameAsset(renameWithBadUUID, scene.assetRegistry).has_value());
  EXPECT_FALSE(edits::commandForRemoveAsset(removeWithBadUUID, scene.assetRegistry).has_value());
}
