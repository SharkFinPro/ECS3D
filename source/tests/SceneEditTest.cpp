#include <gtest/gtest.h>

#include "TestScene.h"
#include "Replication.h"
#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"

#include <cmath>
#include <cstddef>
#include <glm/vec3.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <uuid.h>
#include <vector>

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

  // A prefab body two levels deep: root, one child, one grandchild. Enough to straddle the depth limit
  // without a whole chainOfDepth loop - this suite only ever needs the two nested levels.
  nlohmann::json twoLevelBody()
  {
    nlohmann::json grandchild = trivialBody();
    grandchild["name"] = "Grandchild";

    const nlohmann::json child = {
      { "name", "Child" },
      { "uuid", uuids::to_string(someOtherUUID()) },
      { "components", nlohmann::json::array() },
      { "scripts", nlohmann::json::array() },
      { "children", nlohmann::json::array({ grandchild }) }
    };

    return {
      { "name", "Root" },
      { "uuid", uuids::to_string(someOtherUUID()) },
      { "components", nlohmann::json::array() },
      { "scripts", nlohmann::json::array() },
      { "children", nlohmann::json::array({ child }) }
    };
  }

  // The node `levels` steps below start, chained through addChildObject - the same shape ObjectSpawnTest
  // uses for its own depth fixtures, kept local here since each suite builds its chain through this
  // suite's own Scene type.
  std::shared_ptr<Object> descendantAtDepth(const Scene& scene, const std::shared_ptr<Object>& start,
                                            const std::size_t levels)
  {
    auto current = start;
    for (std::size_t i = 0; i < levels; ++i)
    {
      current = addChildObject(scene, "Descendant", current);
    }

    return current;
  }

  // The names of a sibling list in order - what every reorderObject assertion below checks, since a
  // uuid-by-uuid comparison would not show a mis-ordering as clearly as a mismatched name sequence does.
  std::vector<std::string> namesOf(const std::vector<std::shared_ptr<Object>>& objects)
  {
    std::vector<std::string> names;
    names.reserve(objects.size());
    for (const auto& object : objects)
    {
      names.push_back(object->getName());
    }

    return names;
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
  // y is not representable (dividing by the parent's zero world scale there is not finite): the
  // object's existing local scale is left as is.
  EXPECT_NEAR(localScaleAfter.y, localScaleBefore.y, 1e-4f);
}

TEST(SceneEdit, ReparentingOntoAParentWithADenormalWorldScaleAxisKeepsThatAxisAndCompensatesOthers)
{
  const auto scene = makeScene();

  // float's largest finite value is about 3.4e38, so any divisor smaller than (numerator / 3.4e38)
  // makes the division overflow to +/-infinity regardless of the numerator's exact value. For an
  // ordinary object scale that threshold sits around 1e-38; 1e-40f is comfortably below it and is
  // itself in the denormal range (below the smallest normal float, ~1.18e-38), so this is chosen by
  // that reasoning rather than by trial and error, and is a different case from the exact-zero one
  // covered above.
  const auto parent = addObject(scene, "Parent", glm::vec3(10.0f, -4.0f, 7.0f), glm::vec3(2.0f, 1e-40f, 3.0f));
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

  expectNear("world position", transformOf(scene.object)->getPosition(), worldPositionBefore);
  expectNear("world rotation", transformOf(scene.object)->getRotation(), worldRotationBefore);

  // x and z divide cleanly against the parent's ordinary scale on those axes.
  EXPECT_NEAR(transformOf(scene.object)->getScale().x, worldScaleBefore.x, 1e-4f);
  EXPECT_NEAR(transformOf(scene.object)->getScale().z, worldScaleBefore.z, 1e-4f);

  const auto localScaleAfter = transformOf(scene.object)->getLocalScale();
  EXPECT_NEAR(localScaleAfter.x, worldScaleBefore.x / 2.0f, 1e-4f);
  EXPECT_NEAR(localScaleAfter.z, worldScaleBefore.z / 3.0f, 1e-4f);

  // y overflows to infinity rather than dividing cleanly, so the guard keeps the existing local
  // value there instead of writing a non-finite scale into the live transform.
  EXPECT_TRUE(std::isfinite(localScaleAfter.y));
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

TEST(SceneEdit, RestoreObjectRoundTripsARemovedSubtreeWithUuidsAndComponentValuesIntact)
{
  const auto scene = makeScene();

  const auto parent = addObject(scene, "Parent");
  const auto first = addChildObject(scene, "First", parent);
  const auto middle = addChildObject(scene, "Middle", parent);
  const auto last = addChildObject(scene, "Last", parent);
  const auto grandchild = addChildObject(scene, "Grandchild", middle);
  fixtures::addBoxCollider(middle)->setScale(glm::vec3(4.0f));

  const auto middleUUID = middle->getUUID();
  const auto grandchildUUID = grandchild->getUUID();
  const auto body = middle->serialize();

  ASSERT_EQ(applyEdit(scene, replication::buildRemoveSubtree(middleUUID)), SceneEditResult::applied);

  // Negative: middle and its grandchild (and their uuids) are gone, and the parent has two children left.
  EXPECT_EQ(scene.objectManager->getObjectByUUID(middleUUID), nullptr);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(grandchildUUID), nullptr);
  ASSERT_EQ(parent->getChildren().size(), 2u);
  EXPECT_EQ(parent->getChildren()[0], first);
  EXPECT_EQ(parent->getChildren()[1], last);

  const auto parentUUID = parent->getUUID();
  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(body, &parentUUID, 1)), SceneEditResult::applied);

  // Positive: the same uuids are back, at the requested sibling index, with the grandchild and the
  // component value that were on the subtree when it was removed.
  ASSERT_EQ(parent->getChildren().size(), 3u);
  const auto restored = parent->getChildren()[1];
  EXPECT_EQ(restored->getUUID(), middleUUID);
  EXPECT_EQ(restored->getName(), "Middle");
  EXPECT_EQ(scene.objectManager->getObjectByUUID(middleUUID), restored);

  ASSERT_EQ(restored->getChildren().size(), 1u);
  EXPECT_EQ(restored->getChildren().front()->getUUID(), grandchildUUID);

  const auto restoredCollider = restored->getComponent<BoxCollider>(ComponentType::collider);
  ASSERT_NE(restoredCollider, nullptr);
  fixtures::expectNear(restoredCollider->getLocalScale(), glm::vec3(4.0f));
}

TEST(SceneEdit, RestoreObjectAtTheRootRespectsIndexAndAnIndexPastTheEndAppends)
{
  const auto scene = makeScene();
  // scene.object already occupies root index 0.

  nlohmann::json frontBody = trivialBody();
  frontBody["uuid"] = uuids::to_string(anotherUUID());
  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(frontBody, nullptr, 0)), SceneEditResult::applied);

  ASSERT_EQ(scene.objectManager->getObjects().size(), 2u);
  EXPECT_EQ(scene.objectManager->getObjects().front()->getUUID(), anotherUUID());
  EXPECT_EQ(scene.objectManager->getObjects().back(), scene.object);

  // Positive control: an index past the end appends rather than throwing or landing somewhere else.
  const auto thirdUUID = uuids::uuid::from_string("00000000-0000-0000-0000-000000000002").value();
  nlohmann::json backBody = trivialBody();
  backBody["uuid"] = uuids::to_string(thirdUUID);
  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(backBody, nullptr, 100)), SceneEditResult::applied);

  ASSERT_EQ(scene.objectManager->getObjects().size(), 3u);
  EXPECT_EQ(scene.objectManager->getObjects().back()->getUUID(), thirdUUID);
}

TEST(SceneEdit, RestoreObjectIsRejectedWhenABodyUuidAlreadyExists)
{
  const auto scene = makeScene();

  nlohmann::json rootCollision = trivialBody();
  rootCollision["uuid"] = uuids::to_string(scene.object->getUUID());

  const auto before = scene.objectManager->getAllObjects().size();
  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(rootCollision, nullptr, 0)),
            SceneEditResult::rejected);
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), before);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(scene.object->getUUID())->getName(), "Object");

  // Same refusal when the colliding uuid is nested rather than on the root.
  nlohmann::json collidingChild = trivialBody();
  collidingChild["uuid"] = uuids::to_string(scene.object->getUUID());
  nlohmann::json nested = trivialBody();
  nested["uuid"] = uuids::to_string(anotherUUID());
  nested["children"] = nlohmann::json::array({ collidingChild });

  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(nested, nullptr, 0)), SceneEditResult::rejected);
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), before);

  // Positive control: restoring the very same body applies once the incumbent is out of the way.
  ASSERT_EQ(applyEdit(scene, replication::buildRemoveSubtree(scene.object->getUUID())), SceneEditResult::applied);
  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(rootCollision, nullptr, 0)), SceneEditResult::applied);
}

// Checking each node's uuid only against the live manager would miss two nodes in the same body sharing
// a uuid that neither one has yet - both would still get registered and corrupt getObjectByUUID.
TEST(SceneEdit, RestoreObjectIsRejectedWhenTwoNodesInTheBodyShareAUuid)
{
  const auto scene = makeScene();

  const auto sharedUUID = anotherUUID();

  nlohmann::json firstChild = trivialBody();
  firstChild["uuid"] = uuids::to_string(sharedUUID);
  nlohmann::json secondChild = trivialBody();
  secondChild["uuid"] = uuids::to_string(sharedUUID);

  nlohmann::json root = trivialBody();
  root["uuid"] = uuids::to_string(uuids::uuid::from_string("00000000-0000-0000-0000-000000000004").value());
  root["children"] = nlohmann::json::array({ firstChild, secondChild });

  const auto before = scene.objectManager->getAllObjects().size();
  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(root, nullptr, 0)), SceneEditResult::rejected);
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), before);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(sharedUUID), nullptr);

  // Positive control: the same shape restores fine once the duplicate child gets a distinct uuid.
  secondChild["uuid"] = uuids::to_string(uuids::uuid::from_string("00000000-0000-0000-0000-000000000005").value());
  root["children"] = nlohmann::json::array({ firstChild, secondChild });

  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(root, nullptr, 0)), SceneEditResult::applied);
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), before + 3);
}

TEST(SceneEdit, ReportsARestoreObjectBodyWithANonArrayChildrenFieldAsMalformed)
{
  const auto scene = makeScene();

  nlohmann::json body = trivialBody();
  body["children"] = "not-an-array";

  const auto before = scene.objectManager->getAllObjects().size();
  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(body, nullptr, 0)), SceneEditResult::malformedEdit);
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), before);
}

TEST(SceneEdit, ReportsARestoreObjectBodyWithoutAUuidAsMalformed)
{
  const auto scene = makeScene();

  nlohmann::json body = trivialBody();
  body.erase("uuid");

  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(body, nullptr, 0)), SceneEditResult::malformedEdit);
}

TEST(SceneEdit, ReportsARestoreObjectBodyWithAnUnparseableUuidAsMalformed)
{
  const auto scene = makeScene();

  nlohmann::json body = trivialBody();
  body["uuid"] = "not-a-uuid";

  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(body, nullptr, 0)), SceneEditResult::malformedEdit);
}

TEST(SceneEdit, ReportsARestoreObjectWithoutAnIndexAsMalformed)
{
  const auto scene = makeScene();

  nlohmann::json edit = replication::buildRestoreObject(trivialBody(), nullptr, 0);
  edit.erase("index");

  EXPECT_EQ(applyEdit(scene, edit), SceneEditResult::malformedEdit);
}

TEST(SceneEdit, ReportsARestoreObjectWithANegativeIndexAsMalformed)
{
  const auto scene = makeScene();

  nlohmann::json edit = replication::buildRestoreObject(trivialBody(), nullptr, 0);
  edit["index"] = -1;

  EXPECT_EQ(applyEdit(scene, edit), SceneEditResult::malformedEdit);
}

TEST(SceneEdit, ReportsARestoreObjectWithAnUnparseableParentAsMalformed)
{
  const auto scene = makeScene();

  nlohmann::json edit = replication::buildRestoreObject(trivialBody(), nullptr, 0);
  edit["parent"] = "not-a-uuid";

  EXPECT_EQ(applyEdit(scene, edit), SceneEditResult::malformedEdit);
}

TEST(SceneEdit, ReportsARestoreObjectParentTheSceneDoesNotHaveAsUnknown)
{
  const auto scene = makeScene();

  const auto missing = anotherUUID();
  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(trivialBody(), &missing, 0)),
            SceneEditResult::unknownObject);

  // Positive control: a parent that does resolve restores fine, proving the refusal above is about the
  // parent field and not something else broken in the edit.
  const auto parentUUID = scene.object->getUUID();
  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(trivialBody(), &parentUUID, 0)),
            SceneEditResult::applied);
}

TEST(SceneEdit, RestoreObjectIsRejectedWhenDepthWouldExceedTheLimit)
{
  const auto scene = makeScene();

  // A body of height 1 (root + one child).
  nlohmann::json childBody = trivialBody();
  childBody["uuid"] = uuids::to_string(anotherUUID());
  nlohmann::json rootBody = trivialBody();
  const auto rootBodyUUID = uuids::uuid::from_string("00000000-0000-0000-0000-000000000003").value();
  rootBody["uuid"] = uuids::to_string(rootBodyUUID);
  rootBody["children"] = nlohmann::json::array({ childBody });

  // scene.object sits at the scene root (depth 0); walked down to maxObjectDepth - 1, a body of height 1
  // dropped there lands its child one step past the limit.
  const auto deepParent = descendantAtDepth(scene, scene.object, maxObjectDepth - 1);
  const auto deepParentUUID = deepParent->getUUID();

  const auto before = scene.objectManager->getAllObjects().size();
  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(rootBody, &deepParentUUID, 0)),
            SceneEditResult::rejected);
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), before);

  // Positive control: the same body one level higher (child landing exactly at maxObjectDepth) fits.
  const auto shallowerParent = descendantAtDepth(scene, scene.object, maxObjectDepth - 2);
  const auto shallowerParentUUID = shallowerParent->getUUID();

  const auto beforeApply = scene.objectManager->getAllObjects().size();
  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(rootBody, &shallowerParentUUID, 0)),
            SceneEditResult::applied);
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), beforeApply + 2);
}

TEST(SceneEdit, RestoreObjectReportsABodyNamingAnUnknownComponentAsFailed)
{
  const auto scene = makeScene();

  const nlohmann::json body = {
    { "name", "Broken" },
    { "uuid", uuids::to_string(anotherUUID()) },
    { "components", nlohmann::json::array({ { { "type", "Nonexistent" } } }) },
    { "scripts", nlohmann::json::array() },
    { "children", nlohmann::json::array() }
  };

  // instantiateUnder throws on a body naming a component this build does not know; restoreSubtree hits
  // the same throw in the same place (the Object constructor), so nothing partial is left behind either.
  const auto before = scene.objectManager->getAllObjects().size();
  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(body, nullptr, 0)), SceneEditResult::failed);
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), before);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(anotherUUID()), nullptr);
}

TEST(SceneEdit, RemoveSubtreeOnARootDeletesItAndAllDescendantsWithoutPromotingAnyone)
{
  const auto scene = makeScene();

  const auto child = addChildObject(scene, "Child", scene.object);
  const auto grandchild = addChildObject(scene, "Grandchild", child);
  const auto otherRoot = addObject(scene, "Other");

  const auto rootUUID = scene.object->getUUID();
  const auto childUUID = child->getUUID();
  const auto grandchildUUID = grandchild->getUUID();

  EXPECT_EQ(applyEdit(scene, replication::buildRemoveSubtree(rootUUID)), SceneEditResult::applied);

  EXPECT_EQ(scene.objectManager->getObjectByUUID(rootUUID), nullptr);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(childUUID), nullptr);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(grandchildUUID), nullptr);

  // Nothing promoted to the scene root: only the untouched other root remains.
  ASSERT_EQ(scene.objectManager->getObjects().size(), 1u);
  EXPECT_EQ(scene.objectManager->getObjects().front(), otherRoot);
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), 1u);
}

TEST(SceneEdit, RemoveSubtreeOnAChildKeepsItsSiblingsInOrder)
{
  const auto scene = makeScene();

  const auto first = addChildObject(scene, "First", scene.object);
  const auto doomed = addChildObject(scene, "Doomed", scene.object);
  const auto grandchild = addChildObject(scene, "Grandchild", doomed);
  const auto last = addChildObject(scene, "Last", scene.object);

  const auto doomedUUID = doomed->getUUID();
  const auto grandchildUUID = grandchild->getUUID();

  EXPECT_EQ(applyEdit(scene, replication::buildRemoveSubtree(doomedUUID)), SceneEditResult::applied);

  EXPECT_EQ(scene.objectManager->getObjectByUUID(doomedUUID), nullptr);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(grandchildUUID), nullptr);

  ASSERT_EQ(scene.object->getChildren().size(), 2u);
  EXPECT_EQ(scene.object->getChildren()[0], first);
  EXPECT_EQ(scene.object->getChildren()[1], last);
}

TEST(SceneEdit, AddsAnObjectUnderAClientChosenUuid)
{
  const auto scene = makeScene();

  const auto chosen = anotherUUID();
  const auto parentUUID = scene.object->getUUID();

  EXPECT_EQ(applyEdit(scene, replication::buildAddObject("Added", &parentUUID, &chosen)),
            SceneEditResult::applied);

  // The whole point of the field: the sender can find what its own edit created without waiting to
  // diff a snapshot.
  const auto added = scene.objectManager->getObjectByUUID(chosen);
  ASSERT_NE(added, nullptr);
  EXPECT_EQ(added->getName(), "Added");
  EXPECT_EQ(added->getParent(), scene.object);
}

TEST(SceneEdit, DuplicatesAnObjectUnderAClientChosenRootUuidAndStillFreshensItsChildren)
{
  const auto scene = makeScene();
  const auto sourceChild = addChildObject(scene, "Child", scene.object);

  const auto chosen = anotherUUID();
  EXPECT_EQ(applyEdit(scene, replication::buildDuplicateObject(scene.object->getUUID(), &chosen)),
            SceneEditResult::applied);

  const auto copy = scene.objectManager->getObjectByUUID(chosen);
  ASSERT_NE(copy, nullptr);
  EXPECT_NE(copy, scene.object);

  // Positive control: the copy really is the duplicate (it carries the source's child), so the uuid
  // above named the duplicate's root rather than something that was never built.
  ASSERT_EQ(copy->getChildren().size(), scene.object->getChildren().size());

  const auto copyChild = copy->getChildren().front();
  EXPECT_NE(copyChild->getUUID(), sourceChild->getUUID());
  EXPECT_NE(copyChild->getUUID(), chosen);
}

TEST(SceneEdit, InstantiatesAPrefabUnderAClientChosenUuid)
{
  const auto scene = makeScene();

  const auto prefabUUID = someOtherUUID();
  AssetRegistry assetRegistry;
  assetRegistry.registerAsset({ .uuid = prefabUUID, .type = AssetType::Prefab, .path = "Block",
                                .body = trivialBody().dump() });

  const auto firstUUID = anotherUUID();
  EXPECT_EQ(applyEdit(scene, replication::buildInstantiatePrefab(prefabUUID, nullptr, &firstUUID),
                      &assetRegistry),
            SceneEditResult::applied);

  const auto first = scene.objectManager->getObjectByUUID(firstUUID);
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(first->getName(), "Block");

  // A second instance of the same prefab under a different uuid is a second object, not a refusal and
  // not the first instance handed back again.
  const auto secondUUID = uuids::uuid::from_string("00000000-0000-0000-0000-000000000002").value();
  EXPECT_EQ(applyEdit(scene, replication::buildInstantiatePrefab(prefabUUID, nullptr, &secondUUID),
                      &assetRegistry),
            SceneEditResult::applied);

  const auto second = scene.objectManager->getObjectByUUID(secondUUID);
  ASSERT_NE(second, nullptr);
  EXPECT_NE(second, first);
}

TEST(SceneEdit, ReportsACreatingOpWhoseChosenUuidDoesNotParseAsMalformed)
{
  const auto scene = makeScene();

  const auto prefabUUID = someOtherUUID();
  AssetRegistry assetRegistry;
  assetRegistry.registerAsset({ .uuid = prefabUUID, .type = AssetType::Prefab, .path = "Block",
                                .body = trivialBody().dump() });

  const auto before = scene.objectManager->getAllObjects().size();

  nlohmann::json addEdit = replication::buildAddObject("Added");
  addEdit["uuid"] = "not-a-uuid";
  EXPECT_EQ(applyEdit(scene, addEdit), SceneEditResult::malformedEdit);

  nlohmann::json duplicateEdit = replication::buildDuplicateObject(scene.object->getUUID());
  duplicateEdit["uuid"] = "not-a-uuid";
  EXPECT_EQ(applyEdit(scene, duplicateEdit), SceneEditResult::malformedEdit);

  nlohmann::json prefabEdit = replication::buildInstantiatePrefab(prefabUUID);
  prefabEdit["uuid"] = "not-a-uuid";
  EXPECT_EQ(applyEdit(scene, prefabEdit, &assetRegistry), SceneEditResult::malformedEdit);

  EXPECT_EQ(scene.objectManager->getAllObjects().size(), before);

  // Positive control: the same three ops with no uuid field at all still apply, so the refusals above
  // are the bad field and not the ops themselves.
  EXPECT_EQ(applyEdit(scene, replication::buildAddObject("Added")), SceneEditResult::applied);
  EXPECT_EQ(applyEdit(scene, replication::buildDuplicateObject(scene.object->getUUID())),
            SceneEditResult::applied);
  EXPECT_EQ(applyEdit(scene, replication::buildInstantiatePrefab(prefabUUID), &assetRegistry),
            SceneEditResult::applied);
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), before + 3);
}

TEST(SceneEdit, RefusesACreatingOpNamingTheNilUuid)
{
  const auto scene = makeScene();

  // A nil uuid parses, so this is a well-formed request rather than a broken payload - but registering
  // an object carrying one has the manager generate a uuid for it instead, so honoring it would leave
  // the sender waiting for an object that never gets the uuid it asked for.
  nlohmann::json addEdit = replication::buildAddObject("Added");
  addEdit["uuid"] = uuids::to_string(uuids::uuid{});
  EXPECT_EQ(applyEdit(scene, addEdit), SceneEditResult::rejected);
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), 1u);

  // Positive control: a real uuid on the same op applies.
  const auto chosen = anotherUUID();
  EXPECT_EQ(applyEdit(scene, replication::buildAddObject("Added", nullptr, &chosen)),
            SceneEditResult::applied);
}

TEST(SceneEdit, RefusesACreatingOpNamingAUuidTheSceneAlreadyHas)
{
  const auto scene = makeScene();

  const auto prefabUUID = someOtherUUID();
  AssetRegistry assetRegistry;
  assetRegistry.registerAsset({ .uuid = prefabUUID, .type = AssetType::Prefab, .path = "Block",
                                .body = trivialBody().dump() });

  const auto taken = scene.object->getUUID();
  const auto before = scene.objectManager->getAllObjects().size();

  EXPECT_EQ(applyEdit(scene, replication::buildAddObject("Added", nullptr, &taken)),
            SceneEditResult::rejected);
  EXPECT_EQ(applyEdit(scene, replication::buildDuplicateObject(scene.object->getUUID(), &taken)),
            SceneEditResult::rejected);
  EXPECT_EQ(applyEdit(scene, replication::buildInstantiatePrefab(prefabUUID, nullptr, &taken), &assetRegistry),
            SceneEditResult::rejected);

  // Refused before anything was built, and the object already holding that uuid is untouched.
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), before);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(taken), scene.object);
  EXPECT_EQ(scene.object->getName(), "Object");
  EXPECT_TRUE(scene.object->getChildren().empty());

  // Positive control: the same op with a uuid nothing in the scene holds applies, so the refusals above
  // are the collision and not the field itself.
  const auto freeUUID = anotherUUID();
  EXPECT_EQ(applyEdit(scene, replication::buildAddObject("Added", nullptr, &freeUUID)),
            SceneEditResult::applied);
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), before + 1);
}

TEST(SceneEdit, ReorderObjectMovesWithinTheSameParentInBothDirections)
{
  const auto scene = makeScene();

  const auto a = addChildObject(scene, "A", scene.object);
  const auto b = addChildObject(scene, "B", scene.object);
  const auto c = addChildObject(scene, "C", scene.object);
  // scene.object's children start as [A, B, C].

  const auto parentUUID = scene.object->getUUID();

  // Forward: move A (index 0) past the end - the post-removal list is [B, C], so index 2 appends it.
  EXPECT_EQ(applyEdit(scene, replication::buildReorderObject(a->getUUID(), &parentUUID, 2)),
            SceneEditResult::applied);
  EXPECT_EQ(namesOf(scene.object->getChildren()), (std::vector<std::string>{ "B", "C", "A" }));

  // Backward: move A (now index 2) back to the front.
  EXPECT_EQ(applyEdit(scene, replication::buildReorderObject(a->getUUID(), &parentUUID, 0)),
            SceneEditResult::applied);
  EXPECT_EQ(namesOf(scene.object->getChildren()), (std::vector<std::string>{ "A", "B", "C" }));
}

TEST(SceneEdit, ReorderObjectMovesToAnotherParentAtIndex)
{
  const auto scene = makeScene();

  const auto sourceParent = addObject(scene, "SourceParent");
  const auto destParent = addObject(scene, "DestParent");
  const auto moved = addChildObject(scene, "Moved", sourceParent);
  addChildObject(scene, "DestA", destParent);
  addChildObject(scene, "DestB", destParent);
  // destParent's children start as [DestA, DestB].

  const auto destUUID = destParent->getUUID();
  EXPECT_EQ(applyEdit(scene, replication::buildReorderObject(moved->getUUID(), &destUUID, 1)),
            SceneEditResult::applied);

  EXPECT_TRUE(sourceParent->getChildren().empty());
  EXPECT_EQ(moved->getParent(), destParent);
  EXPECT_EQ(namesOf(destParent->getChildren()), (std::vector<std::string>{ "DestA", "Moved", "DestB" }));
}

TEST(SceneEdit, ReorderObjectReordersTheRootList)
{
  const auto scene = makeScene();
  // scene.object already occupies root index 0.

  addObject(scene, "Second");
  const auto third = addObject(scene, "Third");
  // Root list starts as [Object, Second, Third].

  EXPECT_EQ(applyEdit(scene, replication::buildReorderObject(third->getUUID(), nullptr, 0)),
            SceneEditResult::applied);

  EXPECT_EQ(namesOf(scene.objectManager->getObjects()),
            (std::vector<std::string>{ "Third", "Object", "Second" }));
}

TEST(SceneEdit, ReportsAReorderIndexPastTheEndOfItsTargetListAsRejected)
{
  const auto scene = makeScene();

  const auto a = addChildObject(scene, "A", scene.object);
  addChildObject(scene, "B", scene.object);
  // scene.object has 2 children; reordering one of them removes it first, so the target list to insert
  // into only ever has 1 slot free past the end.

  const auto parentUUID = scene.object->getUUID();
  EXPECT_EQ(applyEdit(scene, replication::buildReorderObject(a->getUUID(), &parentUUID, 2)),
            SceneEditResult::rejected);
  EXPECT_EQ(namesOf(scene.object->getChildren()), (std::vector<std::string>{ "A", "B" }));

  // Positive control: the same op one index lower - the actual end of the post-removal list - applies.
  EXPECT_EQ(applyEdit(scene, replication::buildReorderObject(a->getUUID(), &parentUUID, 1)),
            SceneEditResult::applied);
  EXPECT_EQ(namesOf(scene.object->getChildren()), (std::vector<std::string>{ "B", "A" }));
}

TEST(SceneEdit, ReportsAReorderThatWouldCycleAsRejected)
{
  const auto scene = makeScene();

  const auto child = addChildObject(scene, "Child", scene.object);
  const auto childUUID = child->getUUID();

  // Dropping an object onto its own descendant, same refusal reparentObject gives for the same shape.
  EXPECT_EQ(applyEdit(scene, replication::buildReorderObject(scene.object->getUUID(), &childUUID, 0)),
            SceneEditResult::rejected);
  EXPECT_EQ(scene.object->getParent(), nullptr);
}

TEST(SceneEdit, ReportsAReorderToItsOwnCurrentSlotAsRejected)
{
  const auto scene = makeScene();

  const auto a = addChildObject(scene, "A", scene.object);
  addChildObject(scene, "B", scene.object);

  // A already sits at index 0 - asking to put it back there changes nothing.
  const auto parentUUID = scene.object->getUUID();
  EXPECT_EQ(applyEdit(scene, replication::buildReorderObject(a->getUUID(), &parentUUID, 0)),
            SceneEditResult::rejected);
  EXPECT_EQ(namesOf(scene.object->getChildren()), (std::vector<std::string>{ "A", "B" }));
}
