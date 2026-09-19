#include <gtest/gtest.h>

#include "TestScene.h"
#include "Replication.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "ObjectManagerFixtures.h"

#include <algorithm>
#include <glm/vec3.hpp>
#include <memory>
#include <string>
#include <uuid.h>
#include <vector>

namespace {
  using namespace objectManagerFixtures;
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
