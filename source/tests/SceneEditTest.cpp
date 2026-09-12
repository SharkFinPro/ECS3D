#include <gtest/gtest.h>

#include "TestScene.h"
#include "Replication.h"
#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"

#include <glm/vec3.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <uuid.h>

namespace {
  using replication::SceneEditResult;
  using fixtures::expectNear;
  using fixtures::transformOf;

  // Every test here edits a scene that already holds an object, so the fixture carries one.
  struct Scene : fixtures::Scene {
    std::shared_ptr<Object> object;
  };

  Scene makeScene()
  {
    Scene scene;
    scene.object = addObject(scene, "Object");

    return scene;
  }

  // Not named "apply": an unqualified call with a nlohmann::json argument finds std::apply by argument
  // lookup, because basic_json is parameterized on std::map/std::vector/std::string and that makes std an
  // associated namespace. It compiles on some standard libraries and not others.
  SceneEditResult applyEdit(const Scene& scene, const nlohmann::json& edit,
                            const AssetRegistry* assetRegistry = nullptr)
  {
    return replication::applySceneEdit(*scene.objectManager, edit, assetRegistry);
  }

  uuids::uuid someOtherUUID()
  {
    return uuids::uuid::from_string("123e4567-e89b-12d3-a456-426614174000").value();
  }

  // A second fixed uuid, distinct from someOtherUUID(), for tests that need two uuids that name different
  // things (e.g. a prefab and a parent that is not in the scene).
  uuids::uuid anotherUUID()
  {
    return uuids::uuid::from_string("00000000-0000-0000-0000-000000000001").value();
  }

  // A trivial but well-formed prefab body: no components/scripts/children, just enough for instantiate to
  // succeed. The uuid field is overwritten by reassignUUIDs on instantiation, so its value doesn't matter.
  nlohmann::json trivialBody()
  {
    return {
      { "name", "Block" },
      { "uuid", uuids::to_string(someOtherUUID()) },
      { "components", nlohmann::json::array() },
      { "scripts", nlohmann::json::array() },
      { "children", nlohmann::json::array() }
    };
  }
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

TEST(SceneEdit, ReparentingOntoANonIdentityParentPreservesWorldPlacement)
{
  const auto scene = makeScene();

  const auto parent = addObject(scene, "Parent", glm::vec3(100.0f, 0.0f, 0.0f), glm::vec3(2.0f, 2.0f, 2.0f));
  transformOf(parent)->setRotation(glm::vec3(0.0f, 90.0f, 0.0f));

  // scene.object starts at the scene root with an identity transform (see makeScene), so its world
  // values equal its local ones before this reparent.
  transformOf(scene.object)->setPosition(glm::vec3(10.0f, 5.0f, -3.0f));
  transformOf(scene.object)->setRotation(glm::vec3(0.0f, 30.0f, 0.0f));
  transformOf(scene.object)->setScale(glm::vec3(1.5f, 1.5f, 1.5f));

  const auto worldPositionBefore = transformOf(scene.object)->getPosition();
  const auto worldRotationBefore = transformOf(scene.object)->getRotation();
  const auto worldScaleBefore = transformOf(scene.object)->getScale();

  const auto parentUUID = parent->getUUID();
  EXPECT_EQ(applyEdit(scene, replication::buildReparentObject(scene.object->getUUID(), &parentUUID)),
            SceneEditResult::applied);
  EXPECT_EQ(scene.object->getParent(), parent);

  // World placement is unchanged by the reparent. Before the fix, this came out as the parent's own
  // position/scale/rotation composed on top of the object's now-stale local values instead.
  expectNear("world position", transformOf(scene.object)->getPosition(), worldPositionBefore);
  expectNear("world rotation", transformOf(scene.object)->getRotation(), worldRotationBefore);
  expectNear("world scale", transformOf(scene.object)->getScale(), worldScaleBefore);

  // The local values were rewritten to compensate for the new parent's own world transform.
  expectNear("local position", transformOf(scene.object)->getLocalPosition(),
             worldPositionBefore - transformOf(parent)->getPosition());
  expectNear("local rotation", transformOf(scene.object)->getLocalRotation(),
             worldRotationBefore - transformOf(parent)->getRotation());
  expectNear("local scale", transformOf(scene.object)->getLocalScale(),
             worldScaleBefore / transformOf(parent)->getScale());
}

TEST(SceneEdit, ReparentingBackToSceneRootPreservesWorldPlacement)
{
  const auto scene = makeScene();

  const auto parent = addObject(scene, "Parent", glm::vec3(100.0f, 0.0f, 0.0f), glm::vec3(2.0f, 2.0f, 2.0f));
  transformOf(parent)->setRotation(glm::vec3(0.0f, 90.0f, 0.0f));

  const auto child = addChildObject(scene, "Child", parent);
  transformOf(child)->setPosition(glm::vec3(1.0f, 2.0f, 3.0f));
  transformOf(child)->setRotation(glm::vec3(0.0f, 10.0f, 0.0f));
  transformOf(child)->setScale(glm::vec3(0.5f, 0.5f, 0.5f));

  const auto worldPositionBefore = transformOf(child)->getPosition();
  const auto worldRotationBefore = transformOf(child)->getRotation();
  const auto worldScaleBefore = transformOf(child)->getScale();

  EXPECT_EQ(applyEdit(scene, replication::buildReparentObject(child->getUUID())), SceneEditResult::applied);
  EXPECT_EQ(child->getParent(), nullptr);

  expectNear("world position", transformOf(child)->getPosition(), worldPositionBefore);
  expectNear("world rotation", transformOf(child)->getRotation(), worldRotationBefore);
  expectNear("world scale", transformOf(child)->getScale(), worldScaleBefore);

  // No parent transform to compose with at the scene root, so local now equals world.
  expectNear("local position", transformOf(child)->getLocalPosition(), worldPositionBefore);
  expectNear("local rotation", transformOf(child)->getLocalRotation(), worldRotationBefore);
  expectNear("local scale", transformOf(child)->getLocalScale(), worldScaleBefore);
}

TEST(SceneEdit, ReparentingBetweenTwoNonIdentityParentsPreservesWorldPlacement)
{
  const auto scene = makeScene();

  const auto parentA = addObject(scene, "ParentA", glm::vec3(10.0f, 0.0f, 0.0f), glm::vec3(2.0f, 2.0f, 2.0f));
  const auto parentB = addObject(scene, "ParentB", glm::vec3(-5.0f, 20.0f, 3.0f), glm::vec3(3.0f, 1.0f, 1.0f));
  transformOf(parentB)->setRotation(glm::vec3(0.0f, 0.0f, 45.0f));

  const auto child = addChildObject(scene, "Child", parentA);
  transformOf(child)->setPosition(glm::vec3(1.0f, 1.0f, 1.0f));

  const auto worldPositionBefore = transformOf(child)->getPosition();
  const auto worldRotationBefore = transformOf(child)->getRotation();
  const auto worldScaleBefore = transformOf(child)->getScale();

  const auto parentBUUID = parentB->getUUID();
  EXPECT_EQ(applyEdit(scene, replication::buildReparentObject(child->getUUID(), &parentBUUID)),
            SceneEditResult::applied);
  EXPECT_EQ(child->getParent(), parentB);

  expectNear("world position", transformOf(child)->getPosition(), worldPositionBefore);
  expectNear("world rotation", transformOf(child)->getRotation(), worldRotationBefore);
  expectNear("world scale", transformOf(child)->getScale(), worldScaleBefore);

  expectNear("local position", transformOf(child)->getLocalPosition(),
             worldPositionBefore - transformOf(parentB)->getPosition());
  expectNear("local rotation", transformOf(child)->getLocalRotation(),
             worldRotationBefore - transformOf(parentB)->getRotation());
  expectNear("local scale", transformOf(child)->getLocalScale(),
             worldScaleBefore / transformOf(parentB)->getScale());
}

TEST(SceneEdit, ReparentingOntoAParentWithAZeroWorldScaleAxisCompensatesTheOtherAxes)
{
  const auto scene = makeScene();

  // The y axis cannot be divided out below - it is left at the object's own pre-reparent local value.
  // A non-zero parent position/rotation is used too, so the position/rotation assertions below also
  // exercise the fix rather than passing by coincidence.
  const auto parent = addObject(scene, "Parent", glm::vec3(10.0f, -4.0f, 7.0f), glm::vec3(2.0f, 0.0f, 3.0f));
  transformOf(parent)->setRotation(glm::vec3(0.0f, 45.0f, 0.0f));

  transformOf(scene.object)->setPosition(glm::vec3(5.0f, 2.0f, -1.0f));
  transformOf(scene.object)->setRotation(glm::vec3(10.0f, 20.0f, 30.0f));
  transformOf(scene.object)->setScale(glm::vec3(4.0f, 6.0f, 8.0f));

  const auto worldPositionBefore = transformOf(scene.object)->getPosition();
  const auto worldRotationBefore = transformOf(scene.object)->getRotation();
  const auto worldScaleBefore = transformOf(scene.object)->getScale();
  const auto localScaleBefore = transformOf(scene.object)->getLocalScale();

  const auto parentUUID = parent->getUUID();
  EXPECT_EQ(applyEdit(scene, replication::buildReparentObject(scene.object->getUUID(), &parentUUID)),
            SceneEditResult::applied);
  EXPECT_EQ(scene.object->getParent(), parent);

  // Position and rotation do not involve a division, so both stay fully preserved.
  expectNear("world position", transformOf(scene.object)->getPosition(), worldPositionBefore);
  expectNear("world rotation", transformOf(scene.object)->getRotation(), worldRotationBefore);

  // x and z divide cleanly against the parent's non-zero scale on those axes, so world scale is
  // preserved there too.
  EXPECT_NEAR(transformOf(scene.object)->getScale().x, worldScaleBefore.x, 1e-4f);
  EXPECT_NEAR(transformOf(scene.object)->getScale().z, worldScaleBefore.z, 1e-4f);

  const auto localScaleAfter = transformOf(scene.object)->getLocalScale();
  EXPECT_NEAR(localScaleAfter.x, worldScaleBefore.x / 2.0f, 1e-4f);
  EXPECT_NEAR(localScaleAfter.z, worldScaleBefore.z / 3.0f, 1e-4f);
  // y cannot be divided (parent world scale is 0 there): the object's existing local scale is left as is.
  EXPECT_NEAR(localScaleAfter.y, localScaleBefore.y, 1e-4f);
}

TEST(SceneEdit, ReparentsAnObjectWithNoTransformWithoutThrowing)
{
  const auto scene = makeScene();

  const auto parent = addObject(scene, "Parent", glm::vec3(100.0f, 0.0f, 0.0f), glm::vec3(2.0f, 2.0f, 2.0f));

  const auto noTransform = std::make_shared<Object>(std::vector<std::shared_ptr<Component>>{}, "NoTransform");
  scene.objectManager->addObject(noTransform);
  ASSERT_EQ(noTransform->getComponent<Transform>(ComponentType::transform), nullptr);

  const auto parentUUID = parent->getUUID();
  SceneEditResult result = SceneEditResult::failed;
  EXPECT_NO_THROW(result = applyEdit(scene, replication::buildReparentObject(noTransform->getUUID(), &parentUUID)));
  EXPECT_EQ(result, SceneEditResult::applied);
  EXPECT_EQ(noTransform->getParent(), parent);
  EXPECT_EQ(noTransform->getComponent<Transform>(ComponentType::transform), nullptr);
}

TEST(SceneEdit, RejectsReparentOntoDescendantAndLeavesTransformUnchanged)
{
  const auto scene = makeScene();

  transformOf(scene.object)->setPosition(glm::vec3(4.0f, 5.0f, 6.0f));
  transformOf(scene.object)->setScale(glm::vec3(2.0f, 2.0f, 2.0f));

  const auto child = addChildObject(scene, "Child", scene.object);
  transformOf(child)->setPosition(glm::vec3(1.0f, 0.0f, 0.0f));

  const auto descendantUUID = child->getUUID();

  // Positive control: dropping an object onto its own descendant is still rejected, and neither the
  // hierarchy nor the transform this fix rewrites are touched by the attempt.
  EXPECT_EQ(applyEdit(scene, replication::buildReparentObject(scene.object->getUUID(), &descendantUUID)),
            SceneEditResult::rejected);
  EXPECT_EQ(scene.object->getParent(), nullptr);
  expectNear("local position", transformOf(scene.object)->getLocalPosition(), glm::vec3(4.0f, 5.0f, 6.0f));
  expectNear("local scale", transformOf(scene.object)->getLocalScale(), glm::vec3(2.0f, 2.0f, 2.0f));
}
