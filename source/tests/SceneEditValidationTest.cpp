#include <gtest/gtest.h>

#include "TestScene.h"
#include "Replication.h"
#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "SceneEditFixtures.h"

#include <memory>
#include <nlohmann/json.hpp>
#include <uuid.h>

namespace {
  using namespace sceneEditFixtures;
}

TEST(SceneEdit, AppliesAnAddObjectEdit)
{
  const auto scene = makeScene();

  EXPECT_EQ(applyEdit(scene, replication::buildAddObject("Added")), SceneEditResult::applied);
  EXPECT_EQ(scene.objectManager->getObjects().size(), 2u);
}

TEST(SceneEdit, ReportsAnEditWithNoOpAsMalformed)
{
  const auto scene = makeScene();

  // edit.at("op") threw here, and the authority's only guard was the run loop's catch - which logs it as
  // a generic bad message and loses which edit it was.
  EXPECT_EQ(applyEdit(scene, nlohmann::json::object()), SceneEditResult::malformedEdit);
}

TEST(SceneEdit, ReportsAnOpNothingHandlesAsMalformed)
{
  const auto scene = makeScene();

  // Carries an object the scene does have, so this reaches the fallthrough at the end rather than
  // failing earlier on a field the unknown op never gets to.
  EXPECT_EQ(applyEdit(scene, nlohmann::json{ { "op", "teleportEverything" },
                                         { "object", uuids::to_string(scene.object->getUUID()) } }),
            SceneEditResult::malformedEdit);
}

TEST(SceneEdit, ReportsAnOpMissingTheFieldItNeedsAsMalformed)
{
  const auto scene = makeScene();

  // The op is understood; the field it reads with at() is simply not there.
  EXPECT_EQ(applyEdit(scene, nlohmann::json{ { "op", "renameObject" },
                                         { "object", uuids::to_string(scene.object->getUUID()) } }),
            SceneEditResult::malformedEdit);
}

TEST(SceneEdit, ReportsAnObjectUuidThatDoesNotParseAsMalformed)
{
  const auto scene = makeScene();

  EXPECT_EQ(applyEdit(scene, nlohmann::json{ { "op", "removeObject" }, { "object", "not-a-uuid" } }),
            SceneEditResult::malformedEdit);
}

TEST(SceneEdit, ReportsAnObjectTheSceneDoesNotHaveAsUnknown)
{
  const auto scene = makeScene();

  EXPECT_EQ(applyEdit(scene, replication::buildRemoveObject(someOtherUUID())), SceneEditResult::unknownObject);
  EXPECT_EQ(scene.objectManager->getObjects().size(), 1u);
}

TEST(SceneEdit, ReportsAComponentTypeThatDoesNotExistAsUnknown)
{
  const auto scene = makeScene();

  EXPECT_EQ(applyEdit(scene, replication::buildAddComponent(scene.object->getUUID(), "Nope")),
            SceneEditResult::unknownComponent);
}

TEST(SceneEdit, ReportsAnAddComponentDataBlobForTheWrongTypeAsMalformed)
{
  const auto scene = makeScene();

  // "data" names RigidBody's own blob shape, not the Transform edit.at("component") asked to create -
  // an editor-role client could send this by mistake or maliciously, so it has to be checked rather than
  // trusted to loadFromJSON, which would happily misread RigidBody's mass/velocity fields as Transform's
  // position/rotation/scale.
  const auto rigidBodyBlob = RigidBody().serialize();

  EXPECT_EQ(applyEdit(scene, replication::buildAddComponent(scene.object->getUUID(), "Transform",
                                                            &rigidBodyBlob)),
            SceneEditResult::malformedEdit);
  EXPECT_FALSE(scene.object->getComponents().contains(ComponentType::rigidBody));
}

// Positive control for the mismatch test above: a data blob whose own type matches "component" is
// accepted and loaded.
TEST(SceneEdit, AppliesAnAddComponentDataBlobForTheMatchingType)
{
  const auto scene = makeScene();

  const auto rigidBodyBlob = RigidBody().serialize();

  EXPECT_EQ(applyEdit(scene, replication::buildAddComponent(scene.object->getUUID(), "RigidBody",
                                                            &rigidBodyBlob)),
            SceneEditResult::applied);
  EXPECT_TRUE(scene.object->getComponents().contains(ComponentType::rigidBody));
}

TEST(SceneEdit, ReportsRemovingAComponentTheObjectDoesNotCarryAsUnknown)
{
  const auto scene = makeScene();

  const auto collider = std::make_shared<BoxCollider>();

  EXPECT_EQ(applyEdit(scene, replication::buildRemoveComponent(scene.object->getUUID(), collider)),
            SceneEditResult::unknownComponent);
}

TEST(SceneEdit, ReportsAReparentThatWouldCycleAsRejected)
{
  const auto scene = makeScene();

  const auto child = std::make_shared<Object>("Child");
  child->setParent(scene.object);
  scene.objectManager->addObject(child);

  const auto descendantUUID = child->getUUID();

  // Dropping an object onto its own descendant. Refused rather than malformed: the editor can legitimately
  // send it, and the authority has nothing to rebuild every view for.
  EXPECT_EQ(applyEdit(scene, replication::buildReparentObject(scene.object->getUUID(), &descendantUUID)),
            SceneEditResult::rejected);
  EXPECT_EQ(scene.object->getParent(), nullptr);
}

TEST(SceneEdit, ReportsAReparentOntoTheCurrentParentAsRejected)
{
  const auto scene = makeScene();

  const auto child = std::make_shared<Object>("Child");
  child->setParent(scene.object);
  scene.objectManager->addObject(child);

  const auto parentUUID = scene.object->getUUID();

  EXPECT_EQ(applyEdit(scene, replication::buildReparentObject(child->getUUID(), &parentUUID)),
            SceneEditResult::rejected);
}

TEST(SceneEdit, ReportsAPrefabWithNoRegistryToResolveItAsUnknown)
{
  const auto scene = makeScene();

  EXPECT_EQ(applyEdit(scene, replication::buildInstantiatePrefab(someOtherUUID())),
            SceneEditResult::unknownAsset);
}

TEST(SceneEdit, ReportsAPrefabUuidTheRegistryDoesNotHaveAsUnknown)
{
  const auto scene = makeScene();
  const AssetRegistry assetRegistry;

  EXPECT_EQ(applyEdit(scene, replication::buildInstantiatePrefab(someOtherUUID()), &assetRegistry),
            SceneEditResult::unknownAsset);
}

TEST(SceneEdit, ReportsAPrefabBodyThisBuildCannotInstantiateAsFailed)
{
  const auto scene = makeScene();

  const nlohmann::json body = {
    { "name", "Block" },
    { "uuid", uuids::to_string(someOtherUUID()) },
    { "components", nlohmann::json::array({ { { "type", "Nonexistent" } } }) },
    { "scripts", nlohmann::json::array() },
    { "children", nlohmann::json::array() }
  };

  AssetRegistry assetRegistry;
  assetRegistry.registerAsset({ .uuid = someOtherUUID(), .type = AssetType::Prefab, .path = "Block",
                                .body = body.dump() });

  // instantiate throws on a body naming a component this build does not know. That used to reach the run
  // loop as an unlabelled bad message; the subtree it had started is unwound either way.
  EXPECT_EQ(applyEdit(scene, replication::buildInstantiatePrefab(someOtherUUID()), &assetRegistry),
            SceneEditResult::failed);
  EXPECT_EQ(scene.objectManager->getObjects().size(), 1u);
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), 1u);
}

TEST(SceneEdit, InstantiatesAPrefabUnderTheObjectItWasDroppedOnto)
{
  const auto scene = makeScene();

  const nlohmann::json body = {
    { "name", "Prefab Root" },
    { "uuid", uuids::to_string(someOtherUUID()) },
    { "components", nlohmann::json::array() },
    { "scripts", nlohmann::json::array() },
    { "children", nlohmann::json::array({ trivialBody() }) }
  };

  const auto prefabUUID = someOtherUUID();
  AssetRegistry assetRegistry;
  assetRegistry.registerAsset({ .uuid = prefabUUID, .type = AssetType::Prefab, .path = "Prefab Root",
                                .body = body.dump() });

  const auto parentUUID = scene.object->getUUID();
  EXPECT_EQ(applyEdit(scene, replication::buildInstantiatePrefab(prefabUUID, &parentUUID), &assetRegistry),
            SceneEditResult::applied);

  ASSERT_EQ(scene.object->getChildren().size(), 1u);
  const auto instanceRoot = scene.object->getChildren().front();
  EXPECT_EQ(instanceRoot->getParent(), scene.object);
  EXPECT_EQ(instanceRoot->getChildren().size(), 1u);

  // The whole subtree (scene.object + instance root + its child) is registered with the manager...
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), 3u);
  // ...but the instance root did not also land at the scene root - only scene.object is there.
  ASSERT_EQ(scene.objectManager->getObjects().size(), 1u);
  EXPECT_EQ(scene.objectManager->getObjects().front(), scene.object);

  // Positive control: the same prefab without a parent lands at the scene root instead, proving the
  // parent above is what put the instance under scene.object rather than it always ending up there.
  EXPECT_EQ(applyEdit(scene, replication::buildInstantiatePrefab(prefabUUID), &assetRegistry),
            SceneEditResult::applied);
  EXPECT_EQ(scene.objectManager->getObjects().size(), 2u);
}

TEST(SceneEdit, ReportsABadInstantiatePrefabParentWithoutInstantiatingAnything)
{
  const auto scene = makeScene();

  const auto prefabUUID = someOtherUUID();
  AssetRegistry assetRegistry;
  assetRegistry.registerAsset({ .uuid = prefabUUID, .type = AssetType::Prefab, .path = "Block",
                                .body = trivialBody().dump() });

  nlohmann::json unparseableParent = replication::buildInstantiatePrefab(prefabUUID);
  unparseableParent["parent"] = "not-a-uuid";
  EXPECT_EQ(applyEdit(scene, unparseableParent, &assetRegistry), SceneEditResult::malformedEdit);

  const auto missingParent = anotherUUID();
  EXPECT_EQ(applyEdit(scene, replication::buildInstantiatePrefab(prefabUUID, &missingParent), &assetRegistry),
            SceneEditResult::unknownObject);

  // Neither bad parent instantiated anything: still just the one object the fixture started with.
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), 1u);

  // Positive control: a parent that does resolve instantiates fine, proving the failures above are about
  // the parent field and not something else broken in the edit.
  const auto parentUUID = scene.object->getUUID();
  EXPECT_EQ(applyEdit(scene, replication::buildInstantiatePrefab(prefabUUID, &parentUUID), &assetRegistry),
            SceneEditResult::applied);
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), 2u);
}

TEST(SceneEdit, RefusesAPrefabDroppedWhereItsBodyWouldExceedTheDepthLimit)
{
  const auto scene = makeScene();

  const auto prefabUUID = someOtherUUID();
  AssetRegistry assetRegistry;
  assetRegistry.registerAsset({ .uuid = prefabUUID, .type = AssetType::Prefab, .path = "Root",
                                .body = twoLevelBody().dump() });

  // scene.object sits at the scene root (depth 0); walked down to one below the limit, the prefab's own
  // root would land exactly at the limit and its grandchild one step past it.
  const auto deepParent = descendantAtDepth(scene, scene.object, maxObjectDepth - 1);
  const auto deepParentUUID = deepParent->getUUID();

  const auto before = scene.objectManager->getAllObjects().size();
  EXPECT_EQ(applyEdit(scene, replication::buildInstantiatePrefab(prefabUUID, &deepParentUUID), &assetRegistry),
            SceneEditResult::failed);
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), before);

  // Positive control: the same prefab two levels higher (root at maxObjectDepth - 2, grandchild landing
  // exactly at maxObjectDepth) fits, proving the refusal above is the depth check firing and not
  // something else wrong with the prefab or the parent.
  const auto shallowerParent = descendantAtDepth(scene, scene.object, maxObjectDepth - 3);
  const auto shallowerParentUUID = shallowerParent->getUUID();

  const auto beforeApply = scene.objectManager->getAllObjects().size();
  EXPECT_EQ(applyEdit(scene, replication::buildInstantiatePrefab(prefabUUID, &shallowerParentUUID),
                      &assetRegistry),
            SceneEditResult::applied);
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), beforeApply + 3);
}

TEST(SceneEdit, ReportsAPayloadThatIsNotAnObjectAsMalformed)
{
  const auto scene = makeScene();

  // Valid JSON, so it gets past the server's parse and reaches here - where at("op") on an array throws
  // a type error rather than a missing-key one.
  EXPECT_EQ(applyEdit(scene, nlohmann::json::array()), SceneEditResult::malformedEdit);
  EXPECT_EQ(applyEdit(scene, nlohmann::json(42)), SceneEditResult::malformedEdit);
}

TEST(SceneEdit, ReportsAnOpWhoseNameIsNotAStringAsMalformed)
{
  const auto scene = makeScene();

  EXPECT_EQ(applyEdit(scene, nlohmann::json{ { "op", 5 } }), SceneEditResult::malformedEdit);
}

TEST(SceneEdit, ReportsAParentTheSceneDoesNotHaveAsUnknown)
{
  const auto scene = makeScene();

  const auto missing = someOtherUUID();

  // Naming a parent that is not there is not the same as naming none: the sender asked for a child of
  // something, and rooting the object instead and reporting applied tells a stale view it is current.
  EXPECT_EQ(applyEdit(scene, replication::buildAddObject("Added", &missing)),
            SceneEditResult::unknownObject);
  EXPECT_EQ(scene.objectManager->getObjects().size(), 1u);
}

TEST(SceneEdit, ReportsAReparentOntoAParentTheSceneDoesNotHaveAsUnknown)
{
  const auto scene = makeScene();

  const auto child = std::make_shared<Object>("Child");
  child->setParent(scene.object);
  scene.objectManager->addObject(child);

  const auto missing = someOtherUUID();

  // The worse half of the same case: falling through to a null parent would have detached the child to
  // the scene root - the opposite of what was asked - and called it applied.
  EXPECT_EQ(applyEdit(scene, replication::buildReparentObject(child->getUUID(), &missing)),
            SceneEditResult::unknownObject);
  EXPECT_EQ(child->getParent(), scene.object);
}

TEST(SceneEdit, ReportsAReparentToTheSceneRootAsApplied)
{
  const auto scene = makeScene();

  const auto child = std::make_shared<Object>("Child");
  child->setParent(scene.object);
  scene.objectManager->addObject(child);

  // Naming no parent at all still means "move to the root", which the unknown-parent check must not
  // have turned into a refusal.
  EXPECT_EQ(applyEdit(scene, replication::buildReparentObject(child->getUUID())),
            SceneEditResult::applied);
  EXPECT_EQ(child->getParent(), nullptr);
}
