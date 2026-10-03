#include <gtest/gtest.h>

#include "TestScene.h"
#include "Replication.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/collisions/BoxCollider.h"
#include "SceneEditFixtures.h"

#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <uuid.h>

namespace {
  using namespace sceneEditFixtures;
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
