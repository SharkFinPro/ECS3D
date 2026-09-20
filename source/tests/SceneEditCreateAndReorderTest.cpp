#include <gtest/gtest.h>

#include "TestScene.h"
#include "Replication.h"
#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "SceneEditFixtures.h"

#include <nlohmann/json.hpp>
#include <string>
#include <uuid.h>
#include <vector>

namespace {
  using namespace sceneEditFixtures;
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
