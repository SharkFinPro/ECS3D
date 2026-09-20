#include <gtest/gtest.h>

#include "TestScene.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Transform.h"
#include "ObjectManagerFixtures.h"

#include <algorithm>
#include <glm/vec3.hpp>
#include <memory>
#include <vector>

namespace {
  using namespace objectManagerFixtures;
}

TEST(ObjectManager, AddObjectRegistersARootObject)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Root");

  EXPECT_EQ(scene.objectManager->getObjectByUUID(object->getUUID()), object);
  ASSERT_EQ(scene.objectManager->getObjects().size(), 1u);
  EXPECT_EQ(scene.objectManager->getObjects().front(), object);
  ASSERT_EQ(scene.objectManager->getAllObjects().size(), 1u);
  EXPECT_EQ(scene.objectManager->getAllObjects().front(), object);
}

TEST(ObjectManager, AChildObjectIsInAllObjectsButNotAmongTheRoots)
{
  const auto scene = makeScene();
  const auto parent = addObject(scene, "Parent");
  const auto child = addChildObject(scene, "Child", parent);

  ASSERT_EQ(scene.objectManager->getAllObjects().size(), 2u);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(child->getUUID()), child);

  // Positive control: the parent is a root, so getObjects() is not simply empty by construction.
  ASSERT_EQ(scene.objectManager->getObjects().size(), 1u);
  EXPECT_EQ(scene.objectManager->getObjects().front(), parent);
}

TEST(ObjectManager, GetObjectByUUIDReturnsNullForAnUnknownUUID)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Root");

  // Positive control proving the lookup actually works before trusting the negative case.
  ASSERT_EQ(scene.objectManager->getObjectByUUID(object->getUUID()), object);

  EXPECT_EQ(scene.objectManager->getObjectByUUID(unknownUUID()), nullptr);
}

TEST(ObjectManager, RemoveObjectDefersDeletionUntilTheDeletePassRuns)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Root");
  const auto uuid = object->getUUID();

  scene.objectManager->removeObject(object);

  // Queued, not yet applied.
  EXPECT_EQ(scene.objectManager->getObjectByUUID(uuid), object);
  EXPECT_EQ(scene.objectManager->getObjects().size(), 1u);
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), 1u);

  scene.objectManager->deleteObjectsMarkedForDeletion();

  EXPECT_EQ(scene.objectManager->getObjectByUUID(uuid), nullptr);
  EXPECT_TRUE(scene.objectManager->getObjects().empty());
  EXPECT_TRUE(scene.objectManager->getAllObjects().empty());
}

// deleteObjectsMarkedForDeletion does not drop a removed object's subtree - it splices the children up
// to the removed object's own parent, the same shape reparent uses. Only the removed node itself goes.
TEST(ObjectManager, DeletingARemovedChildPromotesItsChildrenToItsOwnParent)
{
  const auto scene = makeScene();
  const auto grandparent = addObject(scene, "Grandparent");
  const auto doomed = addChildObject(scene, "Doomed", grandparent);
  const auto grandchild = addChildObject(scene, "Grandchild", doomed);
  const auto sibling = addChildObject(scene, "Sibling", grandparent);

  scene.objectManager->removeObject(doomed);
  scene.objectManager->deleteObjectsMarkedForDeletion();

  EXPECT_EQ(scene.objectManager->getObjectByUUID(doomed->getUUID()), nullptr);
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), 3u);

  EXPECT_EQ(grandchild->getParent(), grandparent);
  ASSERT_EQ(grandparent->getChildren().size(), 2u);
  EXPECT_NE(std::ranges::find(grandparent->getChildren(), grandchild), grandparent->getChildren().end());

  // Positive control: an unrelated sibling of the removed object is left alone.
  EXPECT_EQ(sibling->getParent(), grandparent);
  EXPECT_NE(std::ranges::find(grandparent->getChildren(), sibling), grandparent->getChildren().end());
}

TEST(ObjectManager, DeletingARemovedRootPromotesItsChildrenToTheSceneRoot)
{
  const auto scene = makeScene();
  const auto doomed = addObject(scene, "Doomed");
  const auto child = addChildObject(scene, "Child", doomed);
  const auto unrelatedRoot = addObject(scene, "Unrelated");

  scene.objectManager->removeObject(doomed);
  scene.objectManager->deleteObjectsMarkedForDeletion();

  EXPECT_EQ(scene.objectManager->getObjectByUUID(doomed->getUUID()), nullptr);
  EXPECT_EQ(child->getParent(), nullptr);

  ASSERT_EQ(scene.objectManager->getObjects().size(), 2u);
  EXPECT_NE(std::ranges::find(scene.objectManager->getObjects(), child), scene.objectManager->getObjects().end());

  // Positive control: the other, untouched root is still exactly where it was.
  EXPECT_NE(std::ranges::find(scene.objectManager->getObjects(), unrelatedRoot),
            scene.objectManager->getObjects().end());
}

// The delete confirmation promises children are "kept and moved up a level" - deleting the middle object
// of a three-deep chain must leave the bottom object exactly where it was in the world, not at the
// grandparent's origin.
TEST(ObjectManager, DeletingAnObjectPreservesItsChildrensWorldPosition)
{
  const auto scene = makeScene();
  const auto grandparent = addObject(scene, "A");
  const auto doomed = addChildObject(scene, "B", grandparent);
  fixtures::transformOf(doomed)->setPosition(glm::vec3(10.0f, 0.0f, 0.0f));
  const auto child = addChildObject(scene, "C", doomed);

  // Positive control: before the delete, the child's world position already includes the doomed
  // object's offset, while its own local position is still the origin.
  fixtures::expectNear(fixtures::positionOf(child), glm::vec3(10.0f, 0.0f, 0.0f));
  fixtures::expectNear(fixtures::transformOf(child)->getLocalPosition(), glm::vec3(0.0f, 0.0f, 0.0f));

  scene.objectManager->removeObject(doomed);
  scene.objectManager->deleteObjectsMarkedForDeletion();

  EXPECT_EQ(child->getParent(), grandparent);
  fixtures::expectNear(fixtures::positionOf(child), glm::vec3(10.0f, 0.0f, 0.0f));
  fixtures::expectNear(fixtures::transformOf(child)->getLocalPosition(), glm::vec3(10.0f, 0.0f, 0.0f));
}

TEST(ObjectManager, DeletingARootWithANonIdentityTransformPreservesItsChildsWorldPlacement)
{
  const auto scene = makeScene();
  const auto doomed = addObject(scene, "Doomed", glm::vec3(5.0f, 3.0f, -2.0f), glm::vec3(2.0f, 2.0f, 2.0f));
  fixtures::transformOf(doomed)->setRotation(glm::vec3(0.0f, 45.0f, 0.0f));

  const auto child = addChildObject(scene, "Child", doomed);
  fixtures::transformOf(child)->setPosition(glm::vec3(1.0f, 0.0f, 0.0f));
  fixtures::transformOf(child)->setRotation(glm::vec3(0.0f, 10.0f, 0.0f));

  const auto worldPositionBefore = fixtures::transformOf(child)->getPosition();
  const auto worldRotationBefore = fixtures::transformOf(child)->getRotation();
  const auto worldScaleBefore = fixtures::transformOf(child)->getScale();

  scene.objectManager->removeObject(doomed);
  scene.objectManager->deleteObjectsMarkedForDeletion();

  // Promoted to the scene root, with nothing left of the deleted root's own transform to compose with.
  EXPECT_EQ(child->getParent(), nullptr);
  fixtures::expectNear("world position", fixtures::transformOf(child)->getPosition(), worldPositionBefore);
  fixtures::expectNear("world rotation", fixtures::transformOf(child)->getRotation(), worldRotationBefore);
  fixtures::expectNear("world scale", fixtures::transformOf(child)->getScale(), worldScaleBefore);
}

// The division that compensates scale is against the NEW parent's world scale, not the deleted object's -
// so an axis that is only zero on the grandparent (the child's new parent after B is deleted) is the one
// that has to fall back to the child's own existing local scale, while the axes the grandparent can
// represent are compensated for the scaling B itself used to contribute.
TEST(ObjectManager, DeletingAnObjectCompensatesChildScaleAgainstTheNewParentExceptOnAZeroAxis)
{
  const auto scene = makeScene();
  const auto grandparent = addObject(scene, "Grandparent", glm::vec3(0.0f), glm::vec3(0.0f, 2.0f, 3.0f));
  const auto doomed = addChildObject(scene, "Doomed", grandparent);
  fixtures::transformOf(doomed)->setScale(glm::vec3(5.0f, 2.0f, 4.0f));
  const auto child = addChildObject(scene, "Child", doomed);
  fixtures::transformOf(child)->setScale(glm::vec3(2.0f, 3.0f, 1.0f));

  const auto worldScaleBefore = fixtures::transformOf(child)->getScale();
  const auto localScaleBefore = fixtures::transformOf(child)->getLocalScale();

  scene.objectManager->removeObject(doomed);
  scene.objectManager->deleteObjectsMarkedForDeletion();

  EXPECT_EQ(child->getParent(), grandparent);

  const auto localScaleAfter = fixtures::transformOf(child)->getLocalScale();

  // x is not representable (the grandparent's world scale there is zero), so the child's own existing
  // local x is left untouched rather than compensated.
  EXPECT_NEAR(localScaleAfter.x, localScaleBefore.x, 1e-4f);
  // y and z divide cleanly against the grandparent's own scale there, recovering the scaling the deleted
  // object used to contribute (2 and 4 respectively) rather than leaving it dropped.
  EXPECT_NEAR(localScaleAfter.y, worldScaleBefore.y / 2.0f, 1e-4f);
  EXPECT_NEAR(localScaleAfter.z, worldScaleBefore.z / 3.0f, 1e-4f);

  // y and z of world scale are fully preserved; x cannot be (both the old and new chain multiply through
  // a zero there), so it is left out of this assertion.
  EXPECT_NEAR(fixtures::transformOf(child)->getScale().y, worldScaleBefore.y, 1e-4f);
  EXPECT_NEAR(fixtures::transformOf(child)->getScale().z, worldScaleBefore.z, 1e-4f);
}

TEST(ObjectManager, DeletingAnObjectReparentsAChildWithNoTransformWithoutThrowing)
{
  const auto scene = makeScene();
  const auto grandparent = addObject(scene, "Grandparent");
  const auto doomed = addChildObject(scene, "Doomed", grandparent);

  const auto noTransform = std::make_shared<Object>(std::vector<std::shared_ptr<Component>>{}, "NoTransform");
  noTransform->setParent(doomed);
  scene.objectManager->addObject(noTransform);
  ASSERT_EQ(noTransform->getComponent<Transform>(ComponentType::transform), nullptr);

  scene.objectManager->removeObject(doomed);
  EXPECT_NO_THROW(scene.objectManager->deleteObjectsMarkedForDeletion());

  // Positive control: it still made it under the new parent rather than being dropped by the throw guard.
  EXPECT_EQ(noTransform->getParent(), grandparent);
  EXPECT_NE(std::ranges::find(grandparent->getChildren(), noTransform), grandparent->getChildren().end());
}

TEST(ObjectManager, RemoveObjectReportsWhetherItNewlyMarkedTheObject)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Root");

  EXPECT_TRUE(scene.objectManager->removeObject(object));
  EXPECT_FALSE(scene.objectManager->removeObject(object));

  scene.objectManager->deleteObjectsMarkedForDeletion();

  // A double mark still deletes exactly once - the scene ends up exactly where a single removal leaves
  // it, not doubly-removed or corrupted by revisiting the same object in the delete pass.
  EXPECT_EQ(scene.objectManager->getObjectByUUID(object->getUUID()), nullptr);
  EXPECT_TRUE(scene.objectManager->getObjects().empty());
  EXPECT_TRUE(scene.objectManager->getAllObjects().empty());
}

TEST(ObjectManager, RemovingTheSameObjectTwiceReparentsItsChildOnlyOnce)
{
  const auto scene = makeScene();
  const auto grandparent = addObject(scene, "Grandparent");
  const auto doomed = addChildObject(scene, "Doomed", grandparent);
  const auto grandchild = addChildObject(scene, "Grandchild", doomed);

  EXPECT_TRUE(scene.objectManager->removeObject(doomed));
  EXPECT_FALSE(scene.objectManager->removeObject(doomed));

  scene.objectManager->deleteObjectsMarkedForDeletion();

  // Same end state a single removal produces: the grandchild is promoted exactly once, with exactly one
  // entry for it in the grandparent's children and in the manager's own lists - a second, unrefused pass
  // over the same object would have run removeChild/addChild again and could duplicate or corrupt that.
  EXPECT_EQ(scene.objectManager->getObjectByUUID(doomed->getUUID()), nullptr);
  EXPECT_EQ(grandchild->getParent(), grandparent);

  ASSERT_EQ(grandparent->getChildren().size(), 1u);
  EXPECT_EQ(grandparent->getChildren().front(), grandchild);
  EXPECT_EQ(std::ranges::count(grandparent->getChildren(), grandchild), 1);

  ASSERT_EQ(scene.objectManager->getAllObjects().size(), 2u);
  EXPECT_EQ(std::ranges::count(scene.objectManager->getAllObjects(), grandchild), 1);
}

// Positive control for the two tests above: marking is refused only for an object already queued, not
// for every removeObject call in the tick - two distinct objects removed in the same tick are both
// still marked and both still deleted.
TEST(ObjectManager, RemovingTwoDifferentObjectsInTheSameTickMarksAndDeletesBoth)
{
  const auto scene = makeScene();
  const auto first = addObject(scene, "First");
  const auto second = addObject(scene, "Second");

  EXPECT_TRUE(scene.objectManager->removeObject(first));
  EXPECT_TRUE(scene.objectManager->removeObject(second));

  scene.objectManager->deleteObjectsMarkedForDeletion();

  EXPECT_EQ(scene.objectManager->getObjectByUUID(first->getUUID()), nullptr);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(second->getUUID()), nullptr);
  EXPECT_TRUE(scene.objectManager->getObjects().empty());
  EXPECT_TRUE(scene.objectManager->getAllObjects().empty());
}

// The refusal only holds for the tick that queued the object: deleteObjectsMarkedForDeletion clears
// m_objectsToRemove once it runs, so a fresh object added afterward can be marked normally rather than
// being refused forever.
TEST(ObjectManager, MarkingForDeletionIsScopedToATickNotForever)
{
  const auto scene = makeScene();
  const auto first = addObject(scene, "First");

  EXPECT_TRUE(scene.objectManager->removeObject(first));
  scene.objectManager->deleteObjectsMarkedForDeletion();

  const auto second = addObject(scene, "Second");
  EXPECT_TRUE(scene.objectManager->removeObject(second));

  scene.objectManager->deleteObjectsMarkedForDeletion();

  EXPECT_EQ(scene.objectManager->getObjectByUUID(second->getUUID()), nullptr);
  EXPECT_TRUE(scene.objectManager->getAllObjects().empty());
}
