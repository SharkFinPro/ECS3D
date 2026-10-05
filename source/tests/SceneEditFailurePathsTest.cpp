#include <gtest/gtest.h>

#include "TestScene.h"
#include "Replication.h"
#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "SceneEditFixtures.h"

#include <algorithm>
#include <cstddef>
#include <glm/vec3.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <uuid.h>
#include <vector>

namespace {
  using namespace sceneEditFixtures;

  // Each object's uuid, name, parent and ordered child uuids, sorted so the comparison does not depend on
  // the order objects were registered in.
  std::vector<std::string> shapeOf(const ObjectManager& manager)
  {
    std::vector<std::string> shape;
    for (const auto& object : manager.getAllObjects())
    {
      const auto parent = object->getParent();
      std::string line = uuids::to_string(object->getUUID()) + "|" + object->getName() + "|"
        + (parent ? uuids::to_string(parent->getUUID()) : std::string{}) + "|";

      for (const auto& child : object->getChildren())
      {
        line += uuids::to_string(child->getUUID()) + ",";
      }

      shape.push_back(line);
    }

    std::ranges::sort(shape);
    return shape;
  }

  nlohmann::json withField(nlohmann::json edit, const std::string& key, const nlohmann::json& value)
  {
    edit[key] = value;
    return edit;
  }

  nlohmann::json transformBlobOf(const std::shared_ptr<Object>& object)
  {
    for (const auto& component : object->serialize().at("components"))
    {
      if (component.value("type", std::string{}) == "Transform")
      {
        return component;
      }
    }

    return nlohmann::json::object();
  }

  nlohmann::json adoptEntryFor(const std::shared_ptr<Object>& child, const std::size_t index)
  {
    return { { "object", uuids::to_string(child->getUUID()) }, { "index", index },
             { "transform", transformBlobOf(child) } };
  }

  // Passes every check made before anything mutates (uuids, depth, the adopt list) but cannot be built.
  nlohmann::json unbuildable(nlohmann::json body)
  {
    body["children"] = nlohmann::json::array();
    body["components"].push_back({ { "type", "Nonexistent" } });
    return body;
  }
}

TEST(SceneEditFailurePaths, InstantiatePrefabWithAMalformedPrefabUuidIsMalformed)
{
  const auto scene = makeScene();
  AssetRegistry registry;
  const auto before = shapeOf(*scene.objectManager);

  const nlohmann::json edit = { { "op", "instantiatePrefab" }, { "prefab", "not-a-uuid" } };
  EXPECT_EQ(applyEdit(scene, edit, &registry), SceneEditResult::malformedEdit);
  EXPECT_EQ(shapeOf(*scene.objectManager), before);

  // Positive control: the same op naming a registered prefab applies.
  const auto prefabUUID = someOtherUUID();
  registry.registerAsset({ .uuid = prefabUUID, .type = AssetType::Prefab, .path = "Block",
                           .body = trivialBody().dump() });
  EXPECT_EQ(applyEdit(scene, replication::buildInstantiatePrefab(prefabUUID), &registry),
            SceneEditResult::applied);
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), before.size() + 1);
}

TEST(SceneEditFailurePaths, AddObjectWithAMalformedParentUuidIsMalformed)
{
  const auto scene = makeScene();
  const auto before = shapeOf(*scene.objectManager);

  const auto edit = withField(replication::buildAddObject("Child"), "parent", "not-a-uuid");
  EXPECT_EQ(applyEdit(scene, edit), SceneEditResult::malformedEdit);
  EXPECT_EQ(shapeOf(*scene.objectManager), before);

  const auto parentUUID = scene.object->getUUID();
  EXPECT_EQ(applyEdit(scene, replication::buildAddObject("Child", &parentUUID)), SceneEditResult::applied);
  EXPECT_EQ(scene.object->getChildren().size(), 1u);
}

TEST(SceneEditFailurePaths, RestoreObjectWhoseBodyIsNotAnObjectIsMalformed)
{
  const auto scene = makeScene();
  const auto before = shapeOf(*scene.objectManager);

  const nlohmann::json edit = { { "op", "restoreObject" }, { "body", "not an object" }, { "index", 0 } };
  EXPECT_EQ(applyEdit(scene, edit), SceneEditResult::malformedEdit);
  EXPECT_EQ(shapeOf(*scene.objectManager), before);

  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(trivialBody(), nullptr, 0)),
            SceneEditResult::applied);
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), before.size() + 1);
}

TEST(SceneEditFailurePaths, DuplicateObjectReportsAnObjectItCannotRebuildAsFailed)
{
  // An empty registry cannot create even the Transform the duplicate's blob names, so rebuilding the
  // object from its own serialization throws.
  const fixtures::Scene unusable(fixtures::Components::none);
  const auto source = std::make_shared<Object>("Source");
  unusable.objectManager->addObject(source);
  const auto before = shapeOf(*unusable.objectManager);

  EXPECT_EQ(replication::applySceneEdit(*unusable.objectManager,
                                        replication::buildDuplicateObject(source->getUUID()), nullptr),
            SceneEditResult::failed);
  EXPECT_EQ(shapeOf(*unusable.objectManager), before);

  // Positive control: the same edit on a scene whose registry knows the components applies.
  const auto scene = makeScene();
  EXPECT_EQ(applyEdit(scene, replication::buildDuplicateObject(scene.object->getUUID())),
            SceneEditResult::applied);
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), 2u);
}

TEST(SceneEditFailurePaths, ReparentObjectWithAMalformedParentUuidIsMalformed)
{
  const auto scene = makeScene();
  const auto other = addObject(scene, "Other");
  const auto before = shapeOf(*scene.objectManager);

  const auto edit = withField(replication::buildReparentObject(other->getUUID()), "parent", "not-a-uuid");
  EXPECT_EQ(applyEdit(scene, edit), SceneEditResult::malformedEdit);
  EXPECT_EQ(shapeOf(*scene.objectManager), before);

  const auto parentUUID = scene.object->getUUID();
  EXPECT_EQ(applyEdit(scene, replication::buildReparentObject(other->getUUID(), &parentUUID)),
            SceneEditResult::applied);
  EXPECT_EQ(other->getParent(), scene.object);
}

TEST(SceneEditFailurePaths, ReorderObjectWithAMalformedParentUuidIsMalformed)
{
  const auto scene = makeScene();
  const auto other = addObject(scene, "Other");
  const auto before = shapeOf(*scene.objectManager);

  const auto edit = withField(replication::buildReorderObject(other->getUUID(), nullptr, 0), "parent",
                              "not-a-uuid");
  EXPECT_EQ(applyEdit(scene, edit), SceneEditResult::malformedEdit);
  EXPECT_EQ(shapeOf(*scene.objectManager), before);

  EXPECT_EQ(applyEdit(scene, replication::buildReorderObject(other->getUUID(), nullptr, 0)),
            SceneEditResult::applied);
  EXPECT_EQ(scene.objectManager->getObjects().front(), other);
}

TEST(SceneEditFailurePaths, ReorderObjectNamingAnUnknownParentIsUnknownObject)
{
  const auto scene = makeScene();
  const auto other = addObject(scene, "Other");
  const auto before = shapeOf(*scene.objectManager);

  const auto missing = someOtherUUID();
  EXPECT_EQ(applyEdit(scene, replication::buildReorderObject(other->getUUID(), &missing, 0)),
            SceneEditResult::unknownObject);
  EXPECT_EQ(shapeOf(*scene.objectManager), before);

  const auto parentUUID = scene.object->getUUID();
  EXPECT_EQ(applyEdit(scene, replication::buildReorderObject(other->getUUID(), &parentUUID, 0)),
            SceneEditResult::applied);
  EXPECT_EQ(other->getParent(), scene.object);
}

TEST(SceneEditFailurePaths, ReorderObjectWithAMalformedIndexIsMalformed)
{
  const auto scene = makeScene();
  const auto other = addObject(scene, "Other");
  const auto before = shapeOf(*scene.objectManager);

  const std::vector<nlohmann::json> badIndexes = { "first", -1, 1.5 };
  for (const auto& badIndex : badIndexes)
  {
    const auto edit = withField(replication::buildReorderObject(other->getUUID(), nullptr, 0), "index",
                                badIndex);
    EXPECT_EQ(applyEdit(scene, edit), SceneEditResult::malformedEdit) << badIndex.dump();
    EXPECT_EQ(shapeOf(*scene.objectManager), before);
  }

  EXPECT_EQ(applyEdit(scene, replication::buildReorderObject(other->getUUID(), nullptr, 0)),
            SceneEditResult::applied);
}

TEST(SceneEditFailurePaths, ReorderObjectIntoAParentThatWouldExceedTheDepthLimitIsRejected)
{
  const auto scene = makeScene();
  const auto moved = addObject(scene, "Moved");

  const auto tooDeep = descendantAtDepth(scene, scene.object, maxObjectDepth);
  const auto tooDeepUUID = tooDeep->getUUID();
  const auto before = shapeOf(*scene.objectManager);

  EXPECT_EQ(applyEdit(scene, replication::buildReorderObject(moved->getUUID(), &tooDeepUUID, 0)),
            SceneEditResult::rejected);
  EXPECT_EQ(shapeOf(*scene.objectManager), before);

  // Positive control: one level shallower the object lands exactly at the limit and fits.
  const auto fits = descendantAtDepth(scene, scene.object, maxObjectDepth - 1);
  const auto fitsUUID = fits->getUUID();
  EXPECT_EQ(applyEdit(scene, replication::buildReorderObject(moved->getUUID(), &fitsUUID, 0)),
            SceneEditResult::applied);
  EXPECT_EQ(moved->getParent(), fits);
}

TEST(SceneEditFailurePaths, AddComponentWhoseDataCannotBeLoadedIsFailedAndAddsNothing)
{
  const auto scene = makeScene();
  const auto before = scene.object->serialize().at("components").size();

  // Names the right type, so the identity check passes, but lacks the fields loadFromJSON reads.
  const nlohmann::json broken = { { "type", "RigidBody" } };
  EXPECT_EQ(applyEdit(scene, replication::buildAddComponent(scene.object->getUUID(), "RigidBody", &broken)),
            SceneEditResult::failed);
  EXPECT_EQ(scene.object->getComponent<RigidBody>(ComponentType::rigidBody), nullptr);
  EXPECT_EQ(scene.object->serialize().at("components").size(), before);

  // Positive control: a blob the component wrote itself loads.
  const auto good = RigidBody().serialize();
  EXPECT_EQ(applyEdit(scene, replication::buildAddComponent(scene.object->getUUID(), "RigidBody", &good)),
            SceneEditResult::applied);
  EXPECT_NE(scene.object->getComponent<RigidBody>(ComponentType::rigidBody), nullptr);
}

TEST(SceneEditFailurePaths, RestoreObjectThatFailsToBuildPutsAdoptedChildrenBackUnderTheirParent)
{
  const auto scene = makeScene();

  const auto parent = addObject(scene, "Parent");
  const auto a = addChildObject(scene, "A", parent);
  const auto x = addChildObject(scene, "X", parent);
  const auto b = addChildObject(scene, "B", parent);
  const auto c1 = addChildObject(scene, "C1", x);
  const auto c2 = addChildObject(scene, "C2", x);

  transformOf(x)->setPosition({ 10, 0, 0 });
  transformOf(c1)->setPosition({ 1, 2, 3 });
  transformOf(c2)->setPosition({ 4, 5, 6 });

  const auto body = x->serialize();
  ASSERT_EQ(applyEdit(scene, replication::buildRemoveObject(x->getUUID())), SceneEditResult::applied);
  ASSERT_EQ(parent->getChildren(), (std::vector<std::shared_ptr<Object>>{ a, c1, c2, b }));

  const auto before = shapeOf(*scene.objectManager);
  const auto c1Local = transformOf(c1)->getLocalPosition();
  const auto c2Local = transformOf(c2)->getLocalPosition();

  const nlohmann::json adopt = nlohmann::json::array({ adoptEntryFor(c1, 0), adoptEntryFor(c2, 1) });
  const auto parentUUID = parent->getUUID();
  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(unbuildable(body), &parentUUID, 1, &adopt)),
            SceneEditResult::failed);

  EXPECT_EQ(parent->getChildren(), (std::vector<std::shared_ptr<Object>>{ a, c1, c2, b }));
  EXPECT_EQ(c1->getParent(), parent);
  EXPECT_EQ(c2->getParent(), parent);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(x->getUUID()), nullptr);
  EXPECT_EQ(shapeOf(*scene.objectManager), before);
  expectNear(transformOf(c1)->getLocalPosition(), c1Local);
  expectNear(transformOf(c2)->getLocalPosition(), c2Local);

  // Positive control: the same edit with a buildable body applies, so the failure above was the body.
  nlohmann::json goodBody = body;
  goodBody["children"] = nlohmann::json::array();
  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(goodBody, &parentUUID, 1, &adopt)),
            SceneEditResult::applied);
  ASSERT_NE(c1->getParent(), nullptr);
  EXPECT_EQ(c1->getParent()->getUUID(), x->getUUID());
}

TEST(SceneEditFailurePaths, RestoreObjectThatFailsToBuildAtTheSceneRootPutsAdoptedChildrenBack)
{
  const auto scene = makeScene();

  const auto x = addObject(scene, "X");
  const auto b = addObject(scene, "B");
  const auto c1 = addChildObject(scene, "C1", x);
  transformOf(c1)->setPosition({ 1, 2, 3 });

  const auto body = x->serialize();
  ASSERT_EQ(applyEdit(scene, replication::buildRemoveObject(x->getUUID())), SceneEditResult::applied);
  const auto rootsBefore = scene.objectManager->getObjects();
  ASSERT_EQ(rootsBefore, (std::vector<std::shared_ptr<Object>>{ scene.object, c1, b }));

  const nlohmann::json adopt = nlohmann::json::array({ adoptEntryFor(c1, 0) });
  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(unbuildable(body), nullptr, 1, &adopt)),
            SceneEditResult::failed);

  EXPECT_EQ(scene.objectManager->getObjects(), rootsBefore);
  EXPECT_EQ(c1->getParent(), nullptr);
  expectNear(transformOf(c1)->getLocalPosition(), { 1, 2, 3 });
}
