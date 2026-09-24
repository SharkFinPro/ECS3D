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

  // The Transform component blob out of a serialized object node - what a restoreObject "adopt" entry's
  // own "transform" field carries (see Replication.h's buildRestoreObject).
  nlohmann::json transformComponentOf(const nlohmann::json& objectNode)
  {
    for (const auto& component : objectNode.at("components"))
    {
      if (component.value("type", std::string{}) == "Transform")
      {
        return component;
      }
    }

    return nlohmann::json::object();
  }

  nlohmann::json adoptEntry(const std::shared_ptr<Object>& child, const std::size_t index,
                            const nlohmann::json& childBody)
  {
    return { { "object", uuids::to_string(child->getUUID()) }, { "index", index },
             { "transform", transformComponentOf(childBody) } };
  }
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

// --- restoreObject's "adopt" field: undo of a removeObject, reclaiming the still-live children
// deleteObjectsMarkedForDeletion promoted to the removed object's own parent back under the object this op
// recreates - see AGENTS.md's Editor Undo/Redo section and Replication.h's buildRestoreObject.

TEST(SceneEdit, RestoreObjectWithAdoptReclaimsPromotedChildrenUnderAParentedRestoredObject)
{
  const auto scene = makeScene();

  const auto parent = addObject(scene, "Parent");
  const auto a = addChildObject(scene, "A", parent);
  const auto x = addChildObject(scene, "X", parent);
  const auto b = addChildObject(scene, "B", parent);
  const auto c1 = addChildObject(scene, "C1", x);
  const auto c2 = addChildObject(scene, "C2", x);

  // X itself sits off-origin so promoting c1/c2 to Parent actually rewrites their local values (a
  // WorldPlacement no-op here would hide the bug this adopt list exists to route around).
  transformOf(x)->setPosition({ 10, 0, 0 });
  transformOf(c1)->setPosition({ 1, 2, 3 });
  transformOf(c2)->setPosition({ 4, 5, 6 });

  const auto xUUID = x->getUUID();
  const auto c1UUID = c1->getUUID();
  const auto c2UUID = c2->getUUID();
  const auto body = x->serialize(); // pre-removal: c1/c2 still carry their own local values

  ASSERT_EQ(applyEdit(scene, replication::buildRemoveObject(xUUID)), SceneEditResult::applied);

  // c1/c2 were promoted into X's old slot under Parent, and their local values were rewritten to keep
  // their world placement - no longer {1,2,3}/{4,5,6}.
  ASSERT_EQ(parent->getChildren(), (std::vector<std::shared_ptr<Object>>{ a, c1, c2, b }));
  EXPECT_EQ(c1->getParent(), parent);
  EXPECT_EQ(c2->getParent(), parent);

  nlohmann::json bodyNoChildren = body;
  bodyNoChildren["children"] = nlohmann::json::array();
  const auto& recordedChildren = body.at("children");
  const nlohmann::json adopt = nlohmann::json::array(
    { adoptEntry(c1, 0, recordedChildren.at(0)), adoptEntry(c2, 1, recordedChildren.at(1)) });

  const auto parentUUID = parent->getUUID();
  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(bodyNoChildren, &parentUUID, 1, &adopt)),
            SceneEditResult::applied);

  // X is back at its old index, A and B untouched on either side.
  ASSERT_EQ(parent->getChildren().size(), 3u);
  const auto restoredX = parent->getChildren()[1];
  EXPECT_EQ(restoredX->getUUID(), xUUID);
  EXPECT_EQ(parent->getChildren()[0], a);
  EXPECT_EQ(parent->getChildren()[2], b);

  // c1/c2 are X's children again, in order, at their recorded (pre-removal) local values - not whatever
  // WorldPlacement left them at while they sat promoted under Parent.
  ASSERT_EQ(restoredX->getChildren().size(), 2u);
  EXPECT_EQ(restoredX->getChildren()[0], c1);
  EXPECT_EQ(restoredX->getChildren()[1], c2);
  EXPECT_EQ(c1->getParent(), restoredX);
  EXPECT_EQ(c2->getParent(), restoredX);
  expectNear(transformOf(c1)->getLocalPosition(), { 1, 2, 3 });
  expectNear(transformOf(c2)->getLocalPosition(), { 4, 5, 6 });
  expectNear(transformOf(restoredX)->getLocalPosition(), { 10, 0, 0 });
}

TEST(SceneEdit, RestoreObjectWithAdoptReclaimsPromotedChildrenAtTheSceneRoot)
{
  const auto scene = makeScene();
  // scene.object ("A") already sits at root index 0.

  const auto x = addObject(scene, "X");
  const auto b = addObject(scene, "B");
  const auto c1 = addChildObject(scene, "C1", x);

  transformOf(x)->setPosition({ 10, 0, 0 });
  transformOf(c1)->setPosition({ 1, 2, 3 });

  const auto xUUID = x->getUUID();
  const auto body = x->serialize();

  ASSERT_EQ(applyEdit(scene, replication::buildRemoveObject(xUUID)), SceneEditResult::applied);
  ASSERT_EQ(scene.objectManager->getObjects(),
            (std::vector<std::shared_ptr<Object>>{ scene.object, c1, b }));

  nlohmann::json bodyNoChildren = body;
  bodyNoChildren["children"] = nlohmann::json::array();
  const nlohmann::json adopt = nlohmann::json::array({ adoptEntry(c1, 0, body.at("children").at(0)) });

  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(bodyNoChildren, nullptr, 1, &adopt)),
            SceneEditResult::applied);

  ASSERT_EQ(scene.objectManager->getObjects().size(), 3u);
  const auto restoredX = scene.objectManager->getObjects()[1];
  EXPECT_EQ(restoredX->getUUID(), xUUID);
  EXPECT_EQ(scene.objectManager->getObjects()[0], scene.object);
  EXPECT_EQ(scene.objectManager->getObjects()[2], b);

  ASSERT_EQ(restoredX->getChildren().size(), 1u);
  EXPECT_EQ(restoredX->getChildren().front(), c1);
  EXPECT_EQ(c1->getParent(), restoredX);
  expectNear(transformOf(c1)->getLocalPosition(), { 1, 2, 3 });
}

TEST(SceneEdit, RestoreObjectWithoutAdoptStillBehavesExactlyAsBefore)
{
  // Positive control proving the adopt field is purely additive: a restoreObject that omits it applies
  // exactly like the existing (pre-adopt) tests above already exercise.
  const auto scene = makeScene();

  nlohmann::json body = trivialBody();
  body["uuid"] = uuids::to_string(anotherUUID());

  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(body, nullptr, 0)), SceneEditResult::applied);
  EXPECT_NE(scene.objectManager->getObjectByUUID(anotherUUID()), nullptr);
}

TEST(SceneEdit, RestoreObjectWithAdoptIsRejectedWhenAnAdopteeIsMissing)
{
  const auto scene = makeScene();

  const auto parent = addObject(scene, "Parent");
  const auto x = addChildObject(scene, "X", parent);
  const auto c1 = addChildObject(scene, "C1", x);

  const auto xUUID = x->getUUID();
  const auto c1UUID = c1->getUUID();
  const auto body = x->serialize();

  ASSERT_EQ(applyEdit(scene, replication::buildRemoveObject(xUUID)), SceneEditResult::applied);

  nlohmann::json bodyNoChildren = body;
  bodyNoChildren["children"] = nlohmann::json::array();
  const nlohmann::json adopt = nlohmann::json::array({ adoptEntry(c1, 0, body.at("children").at(0)) });
  const auto parentUUID = parent->getUUID();

  // c1 is removed outright (not just reparented) before the restore attempt runs.
  ASSERT_EQ(applyEdit(scene, replication::buildRemoveSubtree(c1UUID)), SceneEditResult::applied);
  ASSERT_EQ(scene.objectManager->getObjectByUUID(c1UUID), nullptr);

  const auto before = scene.objectManager->getAllObjects().size();
  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(bodyNoChildren, &parentUUID, 0, &adopt)),
            SceneEditResult::unknownObject);
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), before);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(xUUID), nullptr);

  // Positive control: with c1 actually there (undoing the removeSubtree above by re-adding an equivalent
  // child under Parent so c1UUID lives again as C1's stand-in), the same adopt shape applies fine.
  const auto standIn = addChildObject(scene, "C1", parent);
  const nlohmann::json standInAdopt =
    nlohmann::json::array({ adoptEntry(standIn, 0, body.at("children").at(0)) });
  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(bodyNoChildren, &parentUUID, 0, &standInAdopt)),
            SceneEditResult::applied);
}

TEST(SceneEdit, RestoreObjectWithAdoptIsRejectedWhenAnAdopteeLivesUnderADifferentParent)
{
  const auto scene = makeScene();

  const auto parent = addObject(scene, "Parent");
  const auto elsewhere = addObject(scene, "Elsewhere");
  const auto x = addChildObject(scene, "X", parent);
  const auto c1 = addChildObject(scene, "C1", x);

  const auto xUUID = x->getUUID();
  const auto body = x->serialize();

  ASSERT_EQ(applyEdit(scene, replication::buildRemoveObject(xUUID)), SceneEditResult::applied);
  ASSERT_EQ(c1->getParent(), parent);

  nlohmann::json bodyNoChildren = body;
  bodyNoChildren["children"] = nlohmann::json::array();
  const nlohmann::json adopt = nlohmann::json::array({ adoptEntry(c1, 0, body.at("children").at(0)) });
  const auto parentUUID = parent->getUUID();

  // Something else moves c1 out from under Parent before the restore attempt runs.
  const auto elsewhereUUID = elsewhere->getUUID();
  ASSERT_EQ(applyEdit(scene, replication::buildReparentObject(c1->getUUID(), &elsewhereUUID)),
            SceneEditResult::applied);
  ASSERT_EQ(c1->getParent(), elsewhere);

  const auto before = scene.objectManager->getAllObjects().size();
  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(bodyNoChildren, &parentUUID, 0, &adopt)),
            SceneEditResult::rejected);
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), before);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(xUUID), nullptr);
  EXPECT_EQ(c1->getParent(), elsewhere);

  // Positive control: move c1 back under Parent and the same adopt shape applies fine.
  ASSERT_EQ(applyEdit(scene, replication::buildReparentObject(c1->getUUID(), &parentUUID)),
            SceneEditResult::applied);
  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(bodyNoChildren, &parentUUID, 0, &adopt)),
            SceneEditResult::applied);
}

TEST(SceneEdit, RestoreObjectWithAdoptIsRejectedWhenTheAdoptListNamesADuplicateUuid)
{
  const auto scene = makeScene();

  const auto parent = addObject(scene, "Parent");
  const auto x = addChildObject(scene, "X", parent);
  const auto c1 = addChildObject(scene, "C1", x);

  const auto xUUID = x->getUUID();
  const auto body = x->serialize();

  ASSERT_EQ(applyEdit(scene, replication::buildRemoveObject(xUUID)), SceneEditResult::applied);

  nlohmann::json bodyNoChildren = body;
  bodyNoChildren["children"] = nlohmann::json::array();
  const auto& childBody = body.at("children").at(0);
  const nlohmann::json duplicateAdopt =
    nlohmann::json::array({ adoptEntry(c1, 0, childBody), adoptEntry(c1, 1, childBody) });
  const auto parentUUID = parent->getUUID();

  const auto before = scene.objectManager->getAllObjects().size();
  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(bodyNoChildren, &parentUUID, 0, &duplicateAdopt)),
            SceneEditResult::rejected);
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), before);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(xUUID), nullptr);
  EXPECT_EQ(c1->getParent(), parent);

  // Positive control: the same shape with the duplicate entry dropped applies fine.
  const nlohmann::json adopt = nlohmann::json::array({ adoptEntry(c1, 0, childBody) });
  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(bodyNoChildren, &parentUUID, 0, &adopt)),
            SceneEditResult::applied);
}

TEST(SceneEdit, RestoreObjectWithAdoptReportsMalformedEntriesWithoutMutatingTheScene)
{
  const auto scene = makeScene();

  const auto parent = addObject(scene, "Parent");
  const auto x = addChildObject(scene, "X", parent);
  const auto c1 = addChildObject(scene, "C1", x);

  const auto xUUID = x->getUUID();
  const auto body = x->serialize();

  ASSERT_EQ(applyEdit(scene, replication::buildRemoveObject(xUUID)), SceneEditResult::applied);

  nlohmann::json bodyNoChildren = body;
  bodyNoChildren["children"] = nlohmann::json::array();
  const auto& childBody = body.at("children").at(0);
  const auto parentUUID = parent->getUUID();
  const auto before = scene.objectManager->getAllObjects().size();

  // "adopt" not an array.
  {
    nlohmann::json edit = replication::buildRestoreObject(bodyNoChildren, &parentUUID, 0);
    edit["adopt"] = "not-an-array";
    EXPECT_EQ(applyEdit(scene, edit), SceneEditResult::malformedEdit);
    EXPECT_EQ(scene.objectManager->getAllObjects().size(), before);
  }

  // An entry missing "index".
  {
    nlohmann::json entry = adoptEntry(c1, 0, childBody);
    entry.erase("index");
    nlohmann::json edit = replication::buildRestoreObject(bodyNoChildren, &parentUUID, 0);
    edit["adopt"] = nlohmann::json::array({ entry });
    EXPECT_EQ(applyEdit(scene, edit), SceneEditResult::malformedEdit);
    EXPECT_EQ(scene.objectManager->getAllObjects().size(), before);
  }

  // An entry whose "object" does not parse as a uuid.
  {
    nlohmann::json entry = adoptEntry(c1, 0, childBody);
    entry["object"] = "not-a-uuid";
    nlohmann::json edit = replication::buildRestoreObject(bodyNoChildren, &parentUUID, 0);
    edit["adopt"] = nlohmann::json::array({ entry });
    EXPECT_EQ(applyEdit(scene, edit), SceneEditResult::malformedEdit);
    EXPECT_EQ(scene.objectManager->getAllObjects().size(), before);
  }

  // An entry whose "transform" is missing the fields Transform::loadFromJSON needs.
  {
    nlohmann::json entry = adoptEntry(c1, 0, childBody);
    entry["transform"] = nlohmann::json::object({ { "type", "Transform" } });
    nlohmann::json edit = replication::buildRestoreObject(bodyNoChildren, &parentUUID, 0);
    edit["adopt"] = nlohmann::json::array({ entry });
    EXPECT_EQ(applyEdit(scene, edit), SceneEditResult::malformedEdit);
    EXPECT_EQ(scene.objectManager->getAllObjects().size(), before);
  }

  EXPECT_EQ(c1->getParent(), parent);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(xUUID), nullptr);

  // Positive control: the same c1, in a well-formed entry, restores fine - proving the refusals above are
  // about the malformed shapes and not something else broken in this setup.
  const nlohmann::json adopt = nlohmann::json::array({ adoptEntry(c1, 0, childBody) });
  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(bodyNoChildren, &parentUUID, 0, &adopt)),
            SceneEditResult::applied);
}

TEST(SceneEdit, RestoreObjectWithAdoptIsRejectedWhenDepthWouldExceedTheLimit)
{
  const auto scene = makeScene();

  // D sits at depth (maxObjectDepth - 3); X is built directly under it, so restoring X back under D gives
  // baseDepth = ancestorDepth(D) + 1 == maxObjectDepth - 2. C1's own subtree (C1 -> Grandchild -> Extra,
  // height 2) makes requiredHeight = max(bodyHeight=0, 1 + subtreeHeight(C1)=2) == 3, landing the combined
  // depth (maxObjectDepth - 2 + 3 == maxObjectDepth + 1) exactly one step past the limit.
  const auto d = descendantAtDepth(scene, scene.object, maxObjectDepth - 3);
  const auto x = addChildObject(scene, "X", d);
  const auto c1 = addChildObject(scene, "C1", x);
  const auto grandchild = addChildObject(scene, "Grandchild", c1);
  const auto extra = addChildObject(scene, "Extra", grandchild);

  const auto xUUID = x->getUUID();
  const auto dUUID = d->getUUID();
  const auto body = x->serialize(); // C1's own Transform blob (all this test's adopt entry needs) is
                                    // unaffected by Extra being removed for the positive control below.

  ASSERT_EQ(applyEdit(scene, replication::buildRemoveObject(xUUID)), SceneEditResult::applied);
  // C1 (with Grandchild/Extra still hanging off it) was promoted to D, not some unrelated node - this is
  // what resolveAdoptList's "adoptee lives under the restore target" check is about to validate.
  ASSERT_EQ(c1->getParent(), d);

  nlohmann::json bodyNoChildren = body;
  bodyNoChildren["children"] = nlohmann::json::array();
  const nlohmann::json adopt = nlohmann::json::array({ adoptEntry(c1, 0, body.at("children").at(0)) });

  const auto before = scene.objectManager->getAllObjects().size();
  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(bodyNoChildren, &dUUID, 0, &adopt)),
            SceneEditResult::rejected);
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), before);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(xUUID), nullptr);
  EXPECT_EQ(c1->getParent(), d);

  // Positive control: drop Extra (C1's subtree height back to 1, requiredHeight back to 2) and the exact
  // same restore fits at the limit - depth is the only thing that changed.
  ASSERT_EQ(applyEdit(scene, replication::buildRemoveSubtree(extra->getUUID())), SceneEditResult::applied);

  const auto beforeApply = scene.objectManager->getAllObjects().size();
  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(bodyNoChildren, &dUUID, 0, &adopt)),
            SceneEditResult::applied);
  // Only X itself is newly created by restoreSubtree - C1 already existed (promoted, never deleted) and
  // is only reattached, not recreated.
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), beforeApply + 1);
  EXPECT_EQ(c1->getParent(), scene.objectManager->getObjectByUUID(xUUID));
}

// A new sibling can arrive at Parent between the deletion and the undo - it is not one of the adoptees, so
// index is still read against "the pre-removal list minus the removed object" (see Replication.h's
// buildRestoreObject): only the adoptee (C1) is detached before X is reinserted at its recorded index, so
// the new sibling simply keeps whatever place it already has in the list.
TEST(SceneEdit, RestoreObjectWithAdoptPlacesTheRestoredObjectAtItsRecordedIndexAroundANewSibling)
{
  const auto scene = makeScene();

  const auto parent = addObject(scene, "Parent");
  const auto a = addChildObject(scene, "A", parent);
  const auto x = addChildObject(scene, "X", parent);
  const auto b = addChildObject(scene, "B", parent);
  const auto c1 = addChildObject(scene, "C1", x);

  const auto xUUID = x->getUUID();
  const auto body = x->serialize();

  ASSERT_EQ(applyEdit(scene, replication::buildRemoveObject(xUUID)), SceneEditResult::applied);
  ASSERT_EQ(parent->getChildren(), (std::vector<std::shared_ptr<Object>>{ a, c1, b }));

  // N arrives after the deletion but before undo runs; addChildObject appends, so it lands at the end.
  const auto n = addChildObject(scene, "N", parent);
  ASSERT_EQ(parent->getChildren(), (std::vector<std::shared_ptr<Object>>{ a, c1, b, n }));

  nlohmann::json bodyNoChildren = body;
  bodyNoChildren["children"] = nlohmann::json::array();
  const nlohmann::json adopt = nlohmann::json::array({ adoptEntry(c1, 0, body.at("children").at(0)) });
  const auto parentUUID = parent->getUUID();

  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(bodyNoChildren, &parentUUID, 1, &adopt)),
            SceneEditResult::applied);

  // X lands at its recorded index (1) in the list with just C1 detached ([A, B, N]), giving [A, X, B, N]:
  // N shifts one slot later rather than pinning X's spot to the far side of it.
  ASSERT_EQ(parent->getChildren().size(), 4u);
  const auto restoredX = parent->getChildren()[1];
  EXPECT_EQ(restoredX->getUUID(), xUUID);
  EXPECT_EQ(parent->getChildren(), (std::vector<std::shared_ptr<Object>>{ a, restoredX, b, n }));
  ASSERT_EQ(restoredX->getChildren().size(), 1u);
  EXPECT_EQ(restoredX->getChildren().front(), c1);
}

TEST(SceneEdit, RestoreObjectWithAdoptReportsANegativeEntryIndexAsMalformed)
{
  const auto scene = makeScene();

  const auto parent = addObject(scene, "Parent");
  const auto x = addChildObject(scene, "X", parent);
  const auto c1 = addChildObject(scene, "C1", x);

  const auto xUUID = x->getUUID();
  const auto body = x->serialize();

  ASSERT_EQ(applyEdit(scene, replication::buildRemoveObject(xUUID)), SceneEditResult::applied);

  nlohmann::json bodyNoChildren = body;
  bodyNoChildren["children"] = nlohmann::json::array();
  const auto& childBody = body.at("children").at(0);
  const auto parentUUID = parent->getUUID();

  nlohmann::json negativeEntry = adoptEntry(c1, 0, childBody);
  negativeEntry["index"] = -1;
  nlohmann::json edit = replication::buildRestoreObject(bodyNoChildren, &parentUUID, 0);
  edit["adopt"] = nlohmann::json::array({ negativeEntry });

  const auto before = scene.objectManager->getAllObjects().size();
  EXPECT_EQ(applyEdit(scene, edit), SceneEditResult::malformedEdit);
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), before);
  EXPECT_EQ(c1->getParent(), parent);

  // Positive control: the same entry with a non-negative index restores fine.
  const nlohmann::json adopt = nlohmann::json::array({ adoptEntry(c1, 0, childBody) });
  EXPECT_EQ(applyEdit(scene, replication::buildRestoreObject(bodyNoChildren, &parentUUID, 0, &adopt)),
            SceneEditResult::applied);
}
