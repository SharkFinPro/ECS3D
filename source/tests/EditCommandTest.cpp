#include <gtest/gtest.h>

#include "TestScene.h"
#include "assets/AssetRegistry.h"
#include "edits/EditCommand.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Transform.h"

#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <uuid.h>
#include <vector>

namespace {
  uuids::uuid testUUID(const int digit)
  {
    const std::string text = std::string("00000000-0000-0000-0000-00000000000") + static_cast<char>('0' + digit);
    return uuids::uuid::from_string(text).value();
  }

  std::string labelOf(const edits::EditCommand& command, const fixtures::Scene& scene,
                      const AssetRegistry* assetRegistry = nullptr)
  {
    return command.describeForMenu(*scene.objectManager, assetRegistry);
  }

  nlohmann::json typed(const std::string& type)
  {
    nlohmann::json component = nlohmann::json::object();
    component["type"] = type;
    return component;
  }
}

// --- primaryUUID: the uuid a refusal that is not about a field conflict names. isReversible() is always
// true today, so no history refusal reaches it; it is public and a future non-reversible kind would, so each
// kind's choice is pinned here.

TEST(EditCommand, PrimaryUUIDNamesTheCreatedOrTargetedUUIDOfEveryKind)
{
  const auto target = testUUID(1);
  const auto other = testUUID(2);
  const auto third = testUUID(3);

  struct Case {
    edits::CommandKind kind;
    edits::EditCommand command;
  };

  const std::vector<Case> cases = {
    { edits::CommandKind::componentEdit,
      edits::EditCommand::componentEdit(target, typed("Transform"), typed("Transform")) },
    { edits::CommandKind::addObject, edits::EditCommand::addObject(target, other, "Thing", 0) },
    { edits::CommandKind::removeObject,
      edits::EditCommand::removeObject(target, other, 0, nlohmann::json::object()) },
    { edits::CommandKind::reparentObject, edits::EditCommand::reparentObject(target, other, third) },
    { edits::CommandKind::reorderObject, edits::EditCommand::reorderObject(target, other, 0, third, 1) },
    { edits::CommandKind::renameObject, edits::EditCommand::renameObject(target, "Before", "After") },
    { edits::CommandKind::addComponent, edits::EditCommand::addComponent(target, "RigidBody") },
    { edits::CommandKind::removeComponent, edits::EditCommand::removeComponent(target, typed("RigidBody")) },
    // The copy and the instance are what a duplicate/instantiate created, not what it was made from.
    { edits::CommandKind::duplicateObject, edits::EditCommand::duplicateObject(other, target, std::nullopt, 0) },
    { edits::CommandKind::instantiatePrefab,
      edits::EditCommand::instantiatePrefab(other, target, std::nullopt, 0, "{}") },
    { edits::CommandKind::addAsset, edits::EditCommand::addAsset(target, AssetType::Model, "a.obj", "", "") },
    { edits::CommandKind::replaceAsset,
      edits::EditCommand::replaceAsset(target, AssetType::Prefab, "P", "", "{}", "P", "", "{}") },
    { edits::CommandKind::renameAsset, edits::EditCommand::renameAsset(target, "Before", "After") },
    { edits::CommandKind::removeAsset,
      edits::EditCommand::removeAsset(target, AssetType::Model, "a.obj", "", "", "") }
  };

  for (const auto& entry : cases)
  {
    SCOPED_TRACE(static_cast<int>(entry.kind));
    EXPECT_EQ(entry.command.kind(), entry.kind);
    EXPECT_EQ(entry.command.primaryUUID(), target);
  }
}

// --- describeForMenu: what a label falls back to when the command's own recorded fields say nothing.

TEST(EditCommand, ADeleteLabelFallsBackWhenTheRecordedSubtreeHasNoUsableName)
{
  const fixtures::Scene scene;

  nlohmann::json named = nlohmann::json::object();
  named["name"] = "Thing";
  EXPECT_EQ(labelOf(edits::EditCommand::removeObject(testUUID(1), std::nullopt, 0, named), scene),
            "Delete Thing");

  nlohmann::json numericName = nlohmann::json::object();
  numericName["name"] = 5;

  EXPECT_EQ(labelOf(edits::EditCommand::removeObject(testUUID(1), std::nullopt, 0, nlohmann::json::object()),
                    scene),
            "Delete Object");
  EXPECT_EQ(labelOf(edits::EditCommand::removeObject(testUUID(1), std::nullopt, 0, numericName), scene),
            "Delete Object");
  EXPECT_EQ(labelOf(edits::EditCommand::removeObject(testUUID(1), std::nullopt, 0, nlohmann::json::array()),
                    scene),
            "Delete Object");
  EXPECT_EQ(labelOf(edits::EditCommand::removeObject(testUUID(1), std::nullopt, 0, nlohmann::json(5)), scene),
            "Delete Object");
}

TEST(EditCommand, ComponentLabelsFallBackToAGenericNameWhenTheDescriptorIsBlank)
{
  const fixtures::Scene scene;
  const auto id = testUUID(1);

  nlohmann::json script = typed("Script");
  script["className"] = "Mover";
  nlohmann::json box = typed("Collider");
  box["subType"] = "Box";

  EXPECT_EQ(labelOf(edits::EditCommand::componentEdit(id, typed("Transform"), typed("Transform")), scene),
            "Edit Transform");
  EXPECT_EQ(labelOf(edits::EditCommand::componentEdit(id, script, script), scene), "Edit Script Mover");
  EXPECT_EQ(labelOf(edits::EditCommand::componentEdit(id, box, box), scene), "Edit Box");

  // A script with no class name is just "Script"; a collider with no subtype, or no type at all, resolves
  // to an empty registry key and reads as a generic component.
  EXPECT_EQ(labelOf(edits::EditCommand::componentEdit(id, typed("Script"), typed("Script")), scene),
            "Edit Script");
  EXPECT_EQ(labelOf(edits::EditCommand::componentEdit(id, typed("Collider"), typed("Collider")), scene),
            "Edit Component");
  EXPECT_EQ(labelOf(edits::EditCommand::componentEdit(id, typed(""), typed("")), scene), "Edit Component");

  EXPECT_EQ(labelOf(edits::EditCommand::addComponent(id, "Script", "Mover"), scene), "Add Script Mover");
  EXPECT_EQ(labelOf(edits::EditCommand::addComponent(id, "RigidBody"), scene), "Add RigidBody");
  EXPECT_EQ(labelOf(edits::EditCommand::addComponent(id, ""), scene), "Add Component");

  EXPECT_EQ(labelOf(edits::EditCommand::removeComponent(id, script), scene), "Remove Script Mover");
  EXPECT_EQ(labelOf(edits::EditCommand::removeComponent(id, typed("")), scene), "Remove Component");
}

TEST(EditCommand, ObjectLabelsUseTheLiveNameOrAGenericOneWhenTheObjectIsGone)
{
  const fixtures::Scene scene;
  const auto live = addObject(scene, "Live");
  const auto gone = testUUID(7);

  EXPECT_EQ(labelOf(edits::EditCommand::reparentObject(live->getUUID(), std::nullopt, std::nullopt), scene),
            "Move Live");
  EXPECT_EQ(labelOf(edits::EditCommand::reorderObject(live->getUUID(), std::nullopt, 0, std::nullopt, 1), scene),
            "Reorder Live");
  EXPECT_EQ(labelOf(edits::EditCommand::duplicateObject(live->getUUID(), testUUID(8), std::nullopt, 1), scene),
            "Duplicate Live");

  EXPECT_EQ(labelOf(edits::EditCommand::reparentObject(gone, std::nullopt, std::nullopt), scene),
            "Move Object");
  EXPECT_EQ(labelOf(edits::EditCommand::reorderObject(gone, std::nullopt, 0, std::nullopt, 1), scene),
            "Reorder Object");
  EXPECT_EQ(labelOf(edits::EditCommand::duplicateObject(gone, testUUID(8), std::nullopt, 1), scene),
            "Duplicate Object");
}

TEST(EditCommand, BlankNamesInACommandReadAsTheGenericKindName)
{
  const fixtures::Scene scene;
  const auto id = testUUID(1);

  EXPECT_EQ(labelOf(edits::EditCommand::addObject(id, std::nullopt, "Thing", 0), scene), "Add Thing");
  EXPECT_EQ(labelOf(edits::EditCommand::addObject(id, std::nullopt, "", 0), scene), "Add Object");

  EXPECT_EQ(labelOf(edits::EditCommand::renameObject(id, "Before", "After"), scene), "Rename After");
  EXPECT_EQ(labelOf(edits::EditCommand::renameObject(id, "Before", ""), scene), "Rename Object");

  EXPECT_EQ(labelOf(edits::EditCommand::renameAsset(id, "Before", "After"), scene), "Rename Asset After");
  EXPECT_EQ(labelOf(edits::EditCommand::renameAsset(id, "Before", ""), scene), "Rename Asset Asset");
}

// A file asset reads as its file name, a script as its class, and a prefab or scene as the name its path
// field carries.
TEST(EditCommand, AssetLabelsNameTheAssetTheWayTheAssetBrowserDoes)
{
  const fixtures::Scene scene;
  const auto id = testUUID(1);

  EXPECT_EQ(labelOf(edits::EditCommand::addAsset(id, AssetType::Model, "models/rock.obj", "", ""), scene),
            "Add Asset rock.obj");
  EXPECT_EQ(labelOf(edits::EditCommand::addAsset(id, AssetType::Script, "scripts/Mover.cs", "Mover", ""), scene),
            "Add Asset Mover");
  EXPECT_EQ(labelOf(edits::EditCommand::addAsset(id, AssetType::Scene, "Level1", "", ""), scene),
            "Add Asset Level1");

  EXPECT_EQ(labelOf(edits::EditCommand::replaceAsset(id, AssetType::Prefab, "Old", "", "{}", "Block", "", "{}"),
                    scene),
            "Replace Asset Block");
  EXPECT_EQ(labelOf(edits::EditCommand::removeAsset(id, AssetType::Texture, "textures/wall/brick.png", "", "", ""),
                    scene),
            "Delete Asset brick.png");
}

// --- A command that cannot be recorded in the first place: the registry key is read straight off the
// component's own blob, so a blob without a usable type is refused loudly rather than recorded as a blank.

TEST(EditCommand, ABlobWithoutAUsableTypeCannotBecomeAComponentCommand)
{
  const auto id = testUUID(1);

  EXPECT_NO_THROW(static_cast<void>(edits::EditCommand::componentEdit(id, typed("Transform"), typed("Transform"))));
  EXPECT_NO_THROW(static_cast<void>(edits::EditCommand::removeComponent(id, typed("RigidBody"))));

  EXPECT_THROW(static_cast<void>(edits::EditCommand::componentEdit(id, typed("Transform"),
                                                                   nlohmann::json::object())),
               nlohmann::json::out_of_range);
  EXPECT_THROW(static_cast<void>(edits::EditCommand::removeComponent(id, nlohmann::json::object())),
               nlohmann::json::out_of_range);

  nlohmann::json numericType = nlohmann::json::object();
  numericType["type"] = 5;
  EXPECT_THROW(static_cast<void>(edits::EditCommand::removeComponent(id, numericType)),
               nlohmann::json::type_error);
}

// --- Undoing a removeObject reclaims its recorded children with their own recorded Transform; a child whose
// recorded blob carries none (a corrupted record) gets an empty blob rather than a failure.

TEST(EditCommand, UndoOfARemoveObjectAdoptsEachRecordedChildWithItsRecordedTransform)
{
  const fixtures::Scene scene;

  nlohmann::json transform = typed("Transform");
  transform["marker"] = 42;

  nlohmann::json withoutTransform = nlohmann::json::object();
  withoutTransform["uuid"] = uuids::to_string(testUUID(3));
  withoutTransform["name"] = "Bare";
  withoutTransform["components"] = nlohmann::json::array({ typed("RigidBody") });

  nlohmann::json withTransform = nlohmann::json::object();
  withTransform["uuid"] = uuids::to_string(testUUID(4));
  withTransform["name"] = "Placed";
  withTransform["components"] = nlohmann::json::array({ typed("RigidBody"), transform });

  nlohmann::json subtree = nlohmann::json::object();
  subtree["uuid"] = uuids::to_string(testUUID(1));
  subtree["name"] = "Removed";
  subtree["components"] = nlohmann::json::array();
  subtree["children"] = nlohmann::json::array({ withoutTransform, withTransform });

  const auto command = edits::EditCommand::removeObject(testUUID(1), std::nullopt, 3, subtree);
  const auto undo = command.buildUndoJSON(*scene.objectManager);

  EXPECT_EQ(undo.at("op"), "restoreObject");
  EXPECT_EQ(undo.at("index"), 3u);
  EXPECT_FALSE(undo.contains("parent"));
  EXPECT_TRUE(undo.at("body").at("children").empty());

  const auto& adopt = undo.at("adopt");
  ASSERT_EQ(adopt.size(), 2u);

  EXPECT_EQ(adopt.at(0).at("object"), uuids::to_string(testUUID(3)));
  EXPECT_EQ(adopt.at(0).at("index"), 0u);
  EXPECT_EQ(adopt.at(0).at("transform"), nlohmann::json::object());

  EXPECT_EQ(adopt.at(1).at("object"), uuids::to_string(testUUID(4)));
  EXPECT_EQ(adopt.at(1).at("index"), 1u);
  EXPECT_EQ(adopt.at(1).at("transform"), transform);
}

// --- Each command builds only the payload form its kind has: asking for the other form is a caller bug.

TEST(EditCommand, BuildingThePayloadFormAKindDoesNotHaveThrowsLogicError)
{
  const fixtures::Scene scene;
  const auto object = addObject(scene, "Object");
  const auto blob = fixtures::transformOf(object)->serialize();

  const auto sceneEditForm = edits::EditCommand::renameObject(object->getUUID(), "Object", "Renamed");
  const auto messageForm = edits::EditCommand::componentEdit(object->getUUID(), blob, blob);
  const auto assetForm = edits::EditCommand::addAsset(testUUID(1), AssetType::Model, "a.obj", "", "");

  ASSERT_EQ(sceneEditForm.payloadForm(), edits::PayloadForm::sceneEdit);
  ASSERT_EQ(messageForm.payloadForm(), edits::PayloadForm::networkMessage);
  ASSERT_EQ(assetForm.payloadForm(), edits::PayloadForm::networkMessage);

  EXPECT_THROW(static_cast<void>(sceneEditForm.buildUndoMessage(*scene.objectManager)), std::logic_error);
  EXPECT_THROW(static_cast<void>(sceneEditForm.buildRedoMessage(*scene.objectManager)), std::logic_error);
  EXPECT_THROW(static_cast<void>(messageForm.buildUndoJSON(*scene.objectManager)), std::logic_error);
  EXPECT_THROW(static_cast<void>(messageForm.buildRedoJSON(*scene.objectManager)), std::logic_error);
  EXPECT_THROW(static_cast<void>(assetForm.buildUndoJSON(*scene.objectManager)), std::logic_error);
  EXPECT_THROW(static_cast<void>(assetForm.buildRedoJSON(*scene.objectManager)), std::logic_error);

  // Positive control: each kind builds the form it does have.
  EXPECT_NO_THROW(static_cast<void>(sceneEditForm.buildUndoJSON(*scene.objectManager)));
  EXPECT_NO_THROW(static_cast<void>(sceneEditForm.buildRedoJSON(*scene.objectManager)));
  EXPECT_NO_THROW(static_cast<void>(messageForm.buildUndoMessage(*scene.objectManager)));
  EXPECT_NO_THROW(static_cast<void>(messageForm.buildRedoMessage(*scene.objectManager)));
  EXPECT_NO_THROW(static_cast<void>(assetForm.buildUndoMessage(*scene.objectManager)));
  EXPECT_NO_THROW(static_cast<void>(assetForm.buildRedoMessage(*scene.objectManager)));
}
