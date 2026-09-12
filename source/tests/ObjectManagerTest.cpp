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
