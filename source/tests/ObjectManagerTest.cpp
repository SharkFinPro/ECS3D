#include <gtest/gtest.h>

#include "TestScene.h"
#include "Replication.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"

#include <algorithm>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <uuid.h>
#include <vector>

namespace {
  using fixtures::addChildObject;
  using fixtures::addObject;
  using fixtures::makeScene;
  using fixtures::Scene;

  uuids::uuid unknownUUID()
  {
    return uuids::uuid::from_string("123e4567-e89b-12d3-a456-426614174000").value();
  }

  // Every uuid in a subtree, root included - used to check a duplicate collides with nothing in the
  // original it was copied from.
  void collectUUIDs(const std::shared_ptr<Object>& object, std::vector<uuids::uuid>& out)
  {
    out.push_back(object->getUUID());

    for (const auto& child : object->getChildren())
    {
      collectUUIDs(child, out);
    }
  }

  // True when no uuid in one set also appears in the other - used to prove two subtrees share no uuids.
  bool disjointUUIDs(const std::vector<uuids::uuid>& a, const std::vector<uuids::uuid>& b)
  {
    return std::ranges::none_of(a, [&b](const auto& uuid) { return std::ranges::find(b, uuid) != b.end(); });
  }

  // True when every uuid in the set is distinct from every other - used to rule out a subtree that
  // reuses the same uuid across its own nodes.
  bool noDuplicateUUIDs(const std::vector<uuids::uuid>& values)
  {
    for (std::size_t i = 0; i < values.size(); ++i)
    {
      for (std::size_t j = i + 1; j < values.size(); ++j)
      {
        if (values[i] == values[j])
        {
          return false;
        }
      }
    }

    return true;
  }

  // Loads a manager's own serialize() output the way SceneAsset::loadObjects does: each root object is
  // reconstructed with its authored uuid, then its children are attached the same way loadChildren does.
  void loadObjects(const Scene& scene, const nlohmann::json& objectsData)
  {
    for (const auto& objectData : objectsData)
    {
      auto object = std::make_shared<Object>(objectData, scene.objectManager.get());
      scene.objectManager->addObject(object);

      if (objectData.contains("children"))
      {
        object->loadChildren(objectData.at("children"));
      }
    }
  }
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

TEST(ObjectManager, DuplicateObjectGivesTheCopyAndItsSubtreeFreshUuids)
{
  const auto scene = makeScene();
  const auto source = addObject(scene, "Source");
  addChildObject(scene, "SourceChild", source);

  std::vector<uuids::uuid> sourceUUIDs;
  collectUUIDs(source, sourceUUIDs);

  scene.objectManager->duplicateObject(source);

  ASSERT_EQ(scene.objectManager->getObjects().size(), 2u);
  const auto copy = scene.objectManager->getObjects().back();
  ASSERT_NE(copy, source);
  ASSERT_EQ(copy->getChildren().size(), 1u);

  std::vector<uuids::uuid> copyUUIDs;
  collectUUIDs(copy, copyUUIDs);

  ASSERT_EQ(copyUUIDs.size(), sourceUUIDs.size());

  // Every uuid in the copy's subtree is fresh: none of them collide with the source subtree, and the
  // two the copy introduces (root + child) are not duplicates of each other either.
  for (const auto& copyUUID : copyUUIDs)
  {
    EXPECT_EQ(std::ranges::find(sourceUUIDs, copyUUID), sourceUUIDs.end());
  }
  EXPECT_NE(copyUUIDs.front(), copyUUIDs.back());
}

TEST(ObjectManager, DuplicateObjectCopiesNameComponentsAndTransformAndLeavesTheSourceUnchanged)
{
  const auto scene = makeScene();
  const auto source = addObject(scene, "Source", glm::vec3(1.0f, 2.0f, 3.0f));
  fixtures::addBoxCollider(source)->setScale(glm::vec3(4.0f));

  scene.objectManager->duplicateObject(source);

  ASSERT_EQ(scene.objectManager->getObjects().size(), 2u);
  const auto copy = scene.objectManager->getObjects().back();

  EXPECT_EQ(copy->getName(), "Source - Copy");
  fixtures::expectNear(fixtures::positionOf(copy), glm::vec3(1.0f, 2.0f, 3.0f));

  const auto copyCollider = copy->getComponent<BoxCollider>(ComponentType::collider);
  ASSERT_NE(copyCollider, nullptr);
  fixtures::expectNear(copyCollider->getLocalScale(), glm::vec3(4.0f));

  // The source itself must come out of this untouched.
  EXPECT_EQ(source->getName(), "Source");
  fixtures::expectNear(fixtures::positionOf(source), glm::vec3(1.0f, 2.0f, 3.0f));
  const auto sourceCollider = source->getComponent<BoxCollider>(ComponentType::collider);
  ASSERT_NE(sourceCollider, nullptr);
  fixtures::expectNear(sourceCollider->getLocalScale(), glm::vec3(4.0f));
}

TEST(ObjectManager, DuplicateObjectPlacesTheCopyBesideTheSourceRatherThanAtTheRoot)
{
  const auto scene = makeScene();
  const auto parent = addObject(scene, "Parent");
  const auto source = addChildObject(scene, "Source", parent);

  scene.objectManager->duplicateObject(source);

  // The copy is a sibling under the same parent, not a new scene root - unlike instantiate, which always
  // lands at the root (see the prefab-instantiation tests below).
  ASSERT_EQ(parent->getChildren().size(), 2u);
  const auto copy = parent->getChildren().back();
  EXPECT_NE(copy, source);
  EXPECT_EQ(copy->getParent(), parent);

  // The copy is a child, not a new root - only "Parent" itself belongs in the root list.
  ASSERT_EQ(scene.objectManager->getObjects().size(), 1u);
  EXPECT_EQ(scene.objectManager->getObjects().front(), parent);
}

TEST(ObjectManager, InstantiateFromSerializedJsonAssignsFreshUuidsAndRegistersTheRootAtSceneRoot)
{
  const auto authoring = makeScene();
  const auto source = addObject(authoring, "Body");
  addChildObject(authoring, "Limb", source);

  std::vector<uuids::uuid> sourceUUIDs;
  collectUUIDs(source, sourceUUIDs);

  const auto body = source->serialize();

  const auto scene = makeScene();
  const auto instance = scene.objectManager->instantiate(body);

  ASSERT_NE(instance, nullptr);
  EXPECT_EQ(instance->getName(), "Body");
  EXPECT_EQ(instance->getParent(), nullptr);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(instance->getUUID()), instance);
  ASSERT_EQ(scene.objectManager->getObjects().size(), 1u);
  EXPECT_EQ(scene.objectManager->getObjects().front(), instance);

  ASSERT_EQ(instance->getChildren().size(), 1u);
  const auto instanceChild = instance->getChildren().front();
  EXPECT_EQ(instanceChild->getName(), "Limb");
  EXPECT_EQ(scene.objectManager->getObjectByUUID(instanceChild->getUUID()), instanceChild);

  std::vector<uuids::uuid> instanceUUIDs;
  collectUUIDs(instance, instanceUUIDs);

  // Check the whole instance subtree against the whole source subtree, not just corresponding nodes -
  // a pairwise root-to-root, child-to-child comparison would miss the instance root colliding with the
  // source child (or vice versa).
  ASSERT_EQ(instanceUUIDs.size(), sourceUUIDs.size());
  EXPECT_TRUE(noDuplicateUUIDs(sourceUUIDs));
  EXPECT_TRUE(noDuplicateUUIDs(instanceUUIDs));
  EXPECT_TRUE(disjointUUIDs(instanceUUIDs, sourceUUIDs));
}

TEST(ObjectManager, InstantiatingTheSameJsonTwiceProducesTwoDistinctUuidSets)
{
  const auto authoring = makeScene();
  const auto source = addObject(authoring, "Body");
  addChildObject(authoring, "Limb", source);

  std::vector<uuids::uuid> sourceUUIDs;
  collectUUIDs(source, sourceUUIDs);

  const auto body = source->serialize();

  const auto scene = makeScene();
  const auto first = scene.objectManager->instantiate(body);
  const auto second = scene.objectManager->instantiate(body);

  ASSERT_EQ(first->getChildren().size(), 1u);
  ASSERT_EQ(second->getChildren().size(), 1u);
  EXPECT_EQ(scene.objectManager->getObjects().size(), 2u);

  std::vector<uuids::uuid> firstUUIDs;
  collectUUIDs(first, firstUUIDs);
  std::vector<uuids::uuid> secondUUIDs;
  collectUUIDs(second, secondUUIDs);

  // Every instance's whole subtree is checked against the source's whole subtree and the other
  // instance's whole subtree, not just node-to-node - a pairwise comparison would miss, say, the first
  // instance's root colliding with the second instance's child.
  ASSERT_EQ(firstUUIDs.size(), sourceUUIDs.size());
  ASSERT_EQ(secondUUIDs.size(), sourceUUIDs.size());
  EXPECT_TRUE(noDuplicateUUIDs(firstUUIDs));
  EXPECT_TRUE(noDuplicateUUIDs(secondUUIDs));
  EXPECT_TRUE(disjointUUIDs(firstUUIDs, sourceUUIDs));
  EXPECT_TRUE(disjointUUIDs(secondUUIDs, sourceUUIDs));
  EXPECT_TRUE(disjointUUIDs(firstUUIDs, secondUUIDs));
}

TEST(ObjectManager, InstantiateUnderGivesTheRootTheRequestedUuidAndItsChildrenFreshOnes)
{
  const auto authoring = makeScene();
  const auto source = addObject(authoring, "Body");
  const auto sourceChild = addChildObject(authoring, "Limb", source);

  const auto body = source->serialize();

  const auto scene = makeScene();
  const auto requested = unknownUUID();
  const auto instance = scene.objectManager->instantiateUnder(body, nullptr, &requested);

  ASSERT_NE(instance, nullptr);
  EXPECT_EQ(instance->getUUID(), requested);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(requested), instance);

  // Only the root takes the requested uuid; the subtree below it is still copied with fresh ones.
  ASSERT_EQ(instance->getChildren().size(), 1u);
  const auto instanceChild = instance->getChildren().front();
  EXPECT_NE(instanceChild->getUUID(), requested);
  EXPECT_NE(instanceChild->getUUID(), sourceChild->getUUID());
}

TEST(ObjectManager, DuplicateObjectGivesTheCopysRootTheRequestedUuid)
{
  const auto scene = makeScene();
  const auto source = addObject(scene, "Source");
  const auto sourceChild = addChildObject(scene, "SourceChild", source);

  const auto requested = unknownUUID();
  scene.objectManager->duplicateObject(source, &requested);

  const auto copy = scene.objectManager->getObjectByUUID(requested);
  ASSERT_NE(copy, nullptr);
  EXPECT_NE(copy, source);
  EXPECT_EQ(copy->getName(), "Source - Copy");

  ASSERT_EQ(copy->getChildren().size(), 1u);
  const auto copyChild = copy->getChildren().front();
  EXPECT_NE(copyChild->getUUID(), sourceChild->getUUID());
  EXPECT_NE(copyChild->getUUID(), requested);

  // The uuid named the copy's root, not the original: the source keeps the uuid it already had.
  EXPECT_NE(source->getUUID(), requested);
}

// Unlike instantiate (a fresh copy for a prefab drop), loading a manager's own serialized form back in
// has to reproduce the same objects, uuids included - that is what makes save/load idempotent.
TEST(ObjectManager, SerializeThenLoadPreservesUuidsAcrossTheWholeSubtree)
{
  const auto original = makeScene();
  const auto root = addObject(original, "Body", glm::vec3(1.0f, 2.0f, 3.0f));
  const auto child = addChildObject(original, "Child", root);

  const auto rootUUID = root->getUUID();
  const auto childUUID = child->getUUID();

  const auto serialized = original.objectManager->serialize();

  const auto loaded = makeScene();
  loadObjects(loaded, serialized.at("objects"));

  const auto loadedRoot = loaded.objectManager->getObjectByUUID(rootUUID);
  ASSERT_NE(loadedRoot, nullptr);
  EXPECT_EQ(loadedRoot->getName(), "Body");
  fixtures::expectNear(fixtures::positionOf(loadedRoot), glm::vec3(1.0f, 2.0f, 3.0f));

  const auto loadedChild = loaded.objectManager->getObjectByUUID(childUUID);
  ASSERT_NE(loadedChild, nullptr);
  EXPECT_EQ(loadedChild->getName(), "Child");
  EXPECT_EQ(loadedChild->getParent(), loadedRoot);
}

TEST(ObjectManager, ReparentingLeavesAllObjectsAndUuidLookupUnaffectedButMovesTheChildBetweenParents)
{
  const auto scene = makeScene();
  const auto parentA = addObject(scene, "ParentA");
  const auto parentB = addObject(scene, "ParentB");
  const auto child = addChildObject(scene, "Child", parentA);

  const auto childUUID = child->getUUID();
  const auto totalBefore = scene.objectManager->getAllObjects().size();

  const auto parentBUUID = parentB->getUUID();
  replication::applySceneEdit(*scene.objectManager, replication::buildReparentObject(childUUID, &parentBUUID));

  EXPECT_EQ(scene.objectManager->getAllObjects().size(), totalBefore);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(childUUID), child);

  EXPECT_TRUE(parentA->getChildren().empty());
  ASSERT_EQ(parentB->getChildren().size(), 1u);
  EXPECT_EQ(parentB->getChildren().front(), child);
}

TEST(ObjectManager, AddChildAtAnIndexInsertsInTheMiddle)
{
  const auto parent = std::make_shared<Object>("Parent");
  const auto first = std::make_shared<Object>("First");
  const auto last = std::make_shared<Object>("Last");
  parent->addChild(first);
  parent->addChild(last);

  const auto middle = std::make_shared<Object>("Middle");
  parent->addChild(middle, 1);

  ASSERT_EQ(parent->getChildren().size(), 3u);
  EXPECT_EQ(parent->getChildren()[0], first);
  EXPECT_EQ(parent->getChildren()[1], middle);
  EXPECT_EQ(parent->getChildren()[2], last);
}

TEST(ObjectManager, AddChildAtIndexZeroInsertsFirst)
{
  const auto parent = std::make_shared<Object>("Parent");
  const auto first = std::make_shared<Object>("First");
  parent->addChild(first);

  const auto front = std::make_shared<Object>("Front");
  parent->addChild(front, 0);

  ASSERT_EQ(parent->getChildren().size(), 2u);
  EXPECT_EQ(parent->getChildren()[0], front);
  EXPECT_EQ(parent->getChildren()[1], first);
}

TEST(ObjectManager, AddChildAtAnIndexPastTheEndAppends)
{
  const auto parent = std::make_shared<Object>("Parent");
  const auto first = std::make_shared<Object>("First");
  parent->addChild(first);

  const auto extra = std::make_shared<Object>("Extra");
  parent->addChild(extra, 100);

  ASSERT_EQ(parent->getChildren().size(), 2u);
  EXPECT_EQ(parent->getChildren()[0], first);
  EXPECT_EQ(parent->getChildren()[1], extra);
}

TEST(ObjectManager, AddObjectToRootAtAnIndexInsertsInTheMiddle)
{
  const auto scene = makeScene();
  const auto first = addObject(scene, "First");
  const auto last = addObject(scene, "Last");

  const auto middle = std::make_shared<Object>("Middle");
  scene.objectManager->addObjectToRoot(middle, 1);

  ASSERT_EQ(scene.objectManager->getObjects().size(), 3u);
  EXPECT_EQ(scene.objectManager->getObjects()[0], first);
  EXPECT_EQ(scene.objectManager->getObjects()[1], middle);
  EXPECT_EQ(scene.objectManager->getObjects()[2], last);
}

TEST(ObjectManager, AddObjectToRootAtAnIndexPastTheEndAppends)
{
  const auto scene = makeScene();
  const auto first = addObject(scene, "First");

  const auto extra = std::make_shared<Object>("Extra");
  scene.objectManager->addObjectToRoot(extra, 100);

  ASSERT_EQ(scene.objectManager->getObjects().size(), 2u);
  EXPECT_EQ(scene.objectManager->getObjects()[0], first);
  EXPECT_EQ(scene.objectManager->getObjects()[1], extra);
}

// ScriptSystem::fixedUpdate/variableUpdate range over getAllObjects() and run script code (which can call
// World.spawn/spawnPrefab) inside the loop - a live reference to m_allObjects, so an addObject that
// appended straight to it could reallocate the vector out from under that range-for. ObjectManager defers
// the append instead while a ScriptPassGuard is alive, exactly like removeObject already defers to
// deleteObjectsMarkedForDeletion; these tests drive that guard directly (ScriptSystem needs the CoreCLR
// host the rest of the suite stays free of).
TEST(ObjectManager, ObjectsAddedDuringAScriptPassDoNotJoinTheListUntilFlushed)
{
  const auto scene = makeScene();
  const auto original = addObject(scene, "Original");

  {
    const ObjectManager::ScriptPassGuard guard(*scene.objectManager);

    const auto spawned = std::make_shared<Object>("Spawned");
    scene.objectManager->addObject(spawned);

    // Deferred: the pass is still "in progress" (the guard is alive), so the object must not be a member
    // of the list a concurrent range-for over getAllObjects() would be iterating.
    EXPECT_EQ(scene.objectManager->getAllObjects().size(), 1u);
    EXPECT_EQ(scene.objectManager->getObjects().size(), 1u);

    // But still immediately usable: a script positioning/looking up the object it just spawned must not
    // have to wait for the flush.
    EXPECT_EQ(scene.objectManager->getObjectByUUID(spawned->getUUID()), spawned);

    // getPendingAdditions() is what a World binding that scans every object (findObjectByName,
    // getAllObjectUuids) has to check too, alongside getAllObjects() - otherwise it disagrees with
    // getObjectByUUID/objectExists about whether this object exists yet.
    ASSERT_EQ(scene.objectManager->getPendingAdditions().size(), 1u);
    EXPECT_EQ(scene.objectManager->getPendingAdditions().front(), spawned);
  }

  scene.objectManager->flushPendingAdditions();

  ASSERT_EQ(scene.objectManager->getAllObjects().size(), 2u);
  ASSERT_EQ(scene.objectManager->getObjects().size(), 2u);
  EXPECT_EQ(scene.objectManager->getObjects().back()->getName(), "Spawned");
  EXPECT_TRUE(scene.objectManager->getPendingAdditions().empty());

  // Positive control: the object that was already there the whole time is untouched by any of this.
  EXPECT_NE(std::ranges::find(scene.objectManager->getAllObjects(), original),
            scene.objectManager->getAllObjects().end());
}

// The exact contract WorldBindings::bindFindObjectByName/bindGetAllObjectUuids rely on: a caller that
// wants a consistent view of "every object that exists right now" during a script pass has to scan
// getAllObjects() and getPendingAdditions() together, not getAllObjects() alone - getObjectByUUID already
// does both (see above), and a name/uuid-listing binding that only checked getAllObjects() would disagree
// with it about an object a script spawned earlier in the very same pass.
TEST(ObjectManager, FindingAnObjectByNameDuringAScriptPassRequiresCheckingBothListsLikeUuidLookupAlreadyDoes)
{
  const auto scene = makeScene();

  const ObjectManager::ScriptPassGuard guard(*scene.objectManager);

  const auto spawned = std::make_shared<Object>("SpawnedByScript");
  scene.objectManager->addObject(spawned);

  auto findByName = [&](const std::string& name) -> std::shared_ptr<Object> {
    for (const auto& object : scene.objectManager->getAllObjects())
    {
      if (object->getName() == name)
      {
        return object;
      }
    }
    for (const auto& object : scene.objectManager->getPendingAdditions())
    {
      if (object->getName() == name)
      {
        return object;
      }
    }
    return nullptr;
  };

  EXPECT_EQ(findByName("SpawnedByScript"), spawned);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(spawned->getUUID()), spawned);
}

// The actual bug: appending to m_allObjects while a range-for over it is in progress can reallocate the
// vector and invalidate every iterator the loop holds - a range-for's begin()/end() are captured once at
// entry, so that reallocation leaves them dangling, UB for the rest of the pass. This uses the exact same
// loop shape ScriptSystem::fixedUpdate/variableUpdate use (a plain range-for over getAllObjects(), not an
// index re-reading size() each step - that shape would instead just hang, growing its own bound by
// exactly the same amount as the index every iteration, which would demonstrate a different bug than the
// one being fixed here). The adds are bounded by the original count precisely so this terminates either
// way: on the fixed code nothing is added to the vector the range-for holds iterators into, so it visits
// the originals and stops; on a hypothetically unguarded addObject, the range-for's cached end() would
// still only cover the originals captured at entry (a reallocation invalidates iterators, it does not
// extend how many the loop was ever going to visit), so an unbounded add-per-visit was never needed to
// make this terminate - it would just make a real defect harder to tell apart from a runaway loop.
TEST(ObjectManager, SpawningOncePerVisitDuringARangeForPassLeavesTheListUnchangedUntilFlush)
{
  const auto scene = makeScene();

  std::vector<std::shared_ptr<Object>> originals;
  for (int i = 0; i < 8; ++i)
  {
    originals.push_back(addObject(scene, "Original" + std::to_string(i)));
  }

  std::size_t visitCount = 0;
  {
    const ObjectManager::ScriptPassGuard guard(*scene.objectManager);

    for (const auto& object : scene.objectManager->getAllObjects())
    {
      (void)object;
      ++visitCount;

      scene.objectManager->addObject(std::make_shared<Object>("Spawned" + std::to_string(visitCount)));

      // Deferred: the list this range-for is holding iterators into must not change size mid-pass, or a
      // real (unguarded) addObject reallocating it out from under the loop is exactly the bug being fixed.
      EXPECT_EQ(scene.objectManager->getAllObjects().size(), originals.size());
    }
  }

  // Every original was visited exactly once - the guarantee a reallocating vector mid range-for would not
  // honor (a moved/reallocated backing store can skip or repeat elements, or worse).
  EXPECT_EQ(visitCount, originals.size());
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), originals.size());

  scene.objectManager->flushPendingAdditions();

  // All new objects are present after flush - deferring them did not drop any.
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), originals.size() * 2);
  for (const auto& original : originals)
  {
    EXPECT_NE(std::ranges::find(scene.objectManager->getAllObjects(), original),
              scene.objectManager->getAllObjects().end());
  }
}

TEST(ObjectManager, SpawningThenDestroyingInTheSamePassRemovesTheObjectOnceBothFlush)
{
  const auto scene = makeScene();

  std::shared_ptr<Object> spawned;
  {
    const ObjectManager::ScriptPassGuard guard(*scene.objectManager);

    spawned = std::make_shared<Object>("Spawned");
    scene.objectManager->addObject(spawned);

    // Destroying it in the very same pass (World.spawnObject immediately followed by World.destroyObject)
    // has to find it via getObjectByUUID while it is still only pending.
    const auto found = scene.objectManager->getObjectByUUID(spawned->getUUID());
    ASSERT_EQ(found, spawned);
    EXPECT_TRUE(scene.objectManager->removeObject(found));
  }

  // Flush order matters: additions have to land before the delete pass runs, or there is nothing there
  // for it to find and remove.
  scene.objectManager->flushPendingAdditions();
  scene.objectManager->deleteObjectsMarkedForDeletion();

  EXPECT_EQ(scene.objectManager->getObjectByUUID(spawned->getUUID()), nullptr);
  EXPECT_TRUE(scene.objectManager->getAllObjects().empty());
  EXPECT_TRUE(scene.objectManager->getObjects().empty());
}

// A prefab spawned mid-pass (World.spawnPrefab) registers its root, then every child, all through
// addObject - each should defer and land in the same relative order once flushed.
TEST(ObjectManager, ASpawnedPrefabsChildJoinsAlongsideItsRootOnFlush)
{
  const auto scene = makeScene();

  std::shared_ptr<Object> root;
  std::shared_ptr<Object> child;
  {
    const ObjectManager::ScriptPassGuard guard(*scene.objectManager);

    root = std::make_shared<Object>("Root");
    scene.objectManager->addObject(root);

    child = std::make_shared<Object>("Child");
    child->setParent(root);
    scene.objectManager->addObject(child);

    // Parenting isn't deferred - only flat-list membership is - so the child is already reachable from its
    // root during the pass, the way instantiateUnder's own tree-building depends on.
    EXPECT_EQ(root->getChildren().size(), 1u);
    EXPECT_EQ(root->getChildren().front(), child);

    EXPECT_TRUE(scene.objectManager->getAllObjects().empty());
  }

  scene.objectManager->flushPendingAdditions();

  ASSERT_EQ(scene.objectManager->getObjects().size(), 1u);
  EXPECT_EQ(scene.objectManager->getObjects().front(), root);
  ASSERT_EQ(scene.objectManager->getAllObjects().size(), 2u);
  EXPECT_EQ(child->getParent(), root);
}

// Non-script callers (editor edits, snapshot unpack, Object::unpack reconciliation) never run inside a
// ScriptPassGuard, so they must keep the old immediate semantics - the positive control proving the guard,
// not addObject itself, is what changed.
TEST(ObjectManager, AddObjectOutsideAScriptPassIsStillImmediate)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Immediate");

  EXPECT_EQ(scene.objectManager->getAllObjects().size(), 1u);
  EXPECT_EQ(scene.objectManager->getObjects().size(), 1u);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(object->getUUID()), object);
}

// restoreSubtree deliberately does not go through instantiate/instantiateUnder's reassignUUIDs: the
// undo history names a removed subtree by its original uuids, and instantiateUnder is the contrasting
// positive control proving these uuids would otherwise change.
TEST(ObjectManager, RestoreSubtreePreservesUuidsUnlikeInstantiateUnder)
{
  const auto authoring = makeScene();
  const auto source = addObject(authoring, "Body");
  addChildObject(authoring, "Limb", source);

  const auto body = source->serialize();
  const auto rootUUID = uuids::uuid::from_string(std::string(body.at("uuid"))).value();
  const auto childUUID =
    uuids::uuid::from_string(std::string(body.at("children").at(0).at("uuid"))).value();

  const auto scene = makeScene();
  const auto restored = scene.objectManager->restoreSubtree(body, nullptr, 0);

  EXPECT_EQ(restored->getUUID(), rootUUID);
  ASSERT_EQ(restored->getChildren().size(), 1u);
  EXPECT_EQ(restored->getChildren().front()->getUUID(), childUUID);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(rootUUID), restored);

  // Positive control / contrast: instantiateUnder on the very same body mints fresh uuids instead, so
  // the preservation above is restoreSubtree's own doing and not something every builder does.
  const auto instantiated = scene.objectManager->instantiateUnder(body, nullptr);
  EXPECT_NE(instantiated->getUUID(), rootUUID);
  ASSERT_EQ(instantiated->getChildren().size(), 1u);
  EXPECT_NE(instantiated->getChildren().front()->getUUID(), childUUID);
}
