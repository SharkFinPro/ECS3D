#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "Replication.h"
#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "objects/components/collisions/BoxCollider.h"

#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <uuid.h>

namespace {
  using replication::SceneEditResult;

  struct Scene {
    std::shared_ptr<ComponentRegistry> componentRegistry = std::make_shared<ComponentRegistry>();
    std::unique_ptr<ObjectManager> objectManager;
    std::shared_ptr<Object> object;
  };

  Scene makeScene()
  {
    Scene scene;
    registerDataComponents(*scene.componentRegistry);
    scene.objectManager = std::make_unique<ObjectManager>(scene.componentRegistry);

    scene.object = std::make_shared<Object>("Object");
    scene.objectManager->addObject(scene.object);

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
