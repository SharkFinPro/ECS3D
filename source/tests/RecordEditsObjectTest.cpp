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

