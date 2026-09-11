#include <gtest/gtest.h>

#include "TestScene.h"
#include "Replication.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"

#include <nlohmann/json.hpp>
#include <cstddef>
#include <memory>

namespace {
  struct Hierarchy : fixtures::Scene {
    std::shared_ptr<Object> grandparent;
    std::shared_ptr<Object> parent;
    std::shared_ptr<Object> child;
    std::shared_ptr<Object> sibling;
  };

  // grandparent -> parent -> child, plus an unrelated second root, all built the way deserialization
  // builds a scene.
  Hierarchy makeHierarchy()
  {
    Hierarchy hierarchy;
    hierarchy.grandparent = addObject(hierarchy, "Grandparent");
    hierarchy.parent = addChildObject(hierarchy, "Parent", hierarchy.grandparent);
    hierarchy.child = addChildObject(hierarchy, "Child", hierarchy.parent);
    hierarchy.sibling = addObject(hierarchy, "Sibling");

    return hierarchy;
  }

  // The invariant every reparent has to preserve: no object can reach itself by walking up.
  [[nodiscard]] bool parentChainsTerminate(const ObjectManager& objectManager)
  {
    const std::size_t limit = objectManager.getAllObjects().size();

    for (const auto& object : objectManager.getAllObjects())
    {
      std::size_t steps = 0;
      for (auto current = object->getParent(); current; current = current->getParent())
      {
        if (++steps > limit)
        {
          return false;
        }
      }
    }

    return true;
  }

  void reparent(const Hierarchy& hierarchy, const std::shared_ptr<Object>& object,
                const std::shared_ptr<Object>& parent)
  {
    if (parent)
    {
      const auto parentUUID = parent->getUUID();
      replication::applySceneEdit(*hierarchy.objectManager,
                                  replication::buildReparentObject(object->getUUID(), &parentUUID));
      return;
    }

    replication::applySceneEdit(*hierarchy.objectManager,
                                replication::buildReparentObject(object->getUUID()));
  }

  // A root with `levels` single-child descendants below it, built in a loop through the same factories
  // makeHierarchy uses. Returns the deepest node, whose ancestor chain is exactly `levels` long.
  std::shared_ptr<Object> chainOfDepth(const Hierarchy& hierarchy, const std::size_t levels)
  {
    auto current = addObject(hierarchy, "ChainRoot");
    for (std::size_t i = 0; i < levels; ++i)
    {
      current = addChildObject(hierarchy, "ChainNode", current);
    }

    return current;
  }
}

TEST(ObjectHierarchy, IsAncestorOfLooksDownwards)
{
  const auto hierarchy = makeHierarchy();

  EXPECT_TRUE(hierarchy.parent->isAncestorOf(hierarchy.child));
  EXPECT_TRUE(hierarchy.grandparent->isAncestorOf(hierarchy.child));

  EXPECT_FALSE(hierarchy.child->isAncestorOf(hierarchy.parent));
  EXPECT_FALSE(hierarchy.child->isAncestorOf(hierarchy.grandparent));
  EXPECT_FALSE(hierarchy.parent->isAncestorOf(hierarchy.parent));
  EXPECT_FALSE(hierarchy.parent->isAncestorOf(nullptr));

  EXPECT_FALSE(hierarchy.grandparent->isAncestorOf(hierarchy.sibling));
  EXPECT_FALSE(hierarchy.sibling->isAncestorOf(hierarchy.child));
}

TEST(ObjectHierarchy, ReparentOntoOwnDescendantIsRejected)
{
  const auto hierarchy = makeHierarchy();

  reparent(hierarchy, hierarchy.parent, hierarchy.child);

  EXPECT_EQ(hierarchy.parent->getParent(), hierarchy.grandparent);
  EXPECT_EQ(hierarchy.child->getParent(), hierarchy.parent);
  EXPECT_TRUE(hierarchy.child->getChildren().empty());
  EXPECT_EQ(hierarchy.objectManager->getObjects().size(), 2u);
  EXPECT_TRUE(parentChainsTerminate(*hierarchy.objectManager));
}

TEST(ObjectHierarchy, ReparentOntoSelfIsRejected)
{
  const auto hierarchy = makeHierarchy();

  reparent(hierarchy, hierarchy.parent, hierarchy.parent);

  EXPECT_EQ(hierarchy.parent->getParent(), hierarchy.grandparent);
  EXPECT_EQ(hierarchy.parent->getChildren().size(), 1u);
  EXPECT_TRUE(parentChainsTerminate(*hierarchy.objectManager));
}

TEST(ObjectHierarchy, ReparentOntoAnAncestorIsAllowed)
{
  const auto hierarchy = makeHierarchy();

  reparent(hierarchy, hierarchy.child, hierarchy.grandparent);

  EXPECT_EQ(hierarchy.child->getParent(), hierarchy.grandparent);
  EXPECT_TRUE(hierarchy.parent->getChildren().empty());
  EXPECT_EQ(hierarchy.grandparent->getChildren().size(), 2u);
  EXPECT_EQ(hierarchy.objectManager->getObjects().size(), 2u);
  EXPECT_TRUE(parentChainsTerminate(*hierarchy.objectManager));
}

TEST(ObjectHierarchy, ReparentOntoAnUnrelatedObjectIsAllowed)
{
  const auto hierarchy = makeHierarchy();

  reparent(hierarchy, hierarchy.child, hierarchy.sibling);

  EXPECT_EQ(hierarchy.child->getParent(), hierarchy.sibling);
  EXPECT_TRUE(hierarchy.parent->getChildren().empty());
  EXPECT_EQ(hierarchy.sibling->getChildren().size(), 1u);
  EXPECT_TRUE(parentChainsTerminate(*hierarchy.objectManager));
}

TEST(ObjectHierarchy, ReparentToRootUpdatesTheRootList)
{
  const auto hierarchy = makeHierarchy();

  reparent(hierarchy, hierarchy.child, nullptr);

  EXPECT_EQ(hierarchy.child->getParent(), nullptr);
  EXPECT_TRUE(hierarchy.parent->getChildren().empty());
  EXPECT_EQ(hierarchy.objectManager->getObjects().size(), 3u);
  EXPECT_EQ(hierarchy.objectManager->getAllObjects().size(), 4u);
  EXPECT_TRUE(parentChainsTerminate(*hierarchy.objectManager));
}

TEST(ObjectHierarchy, ReparentOntoTheCurrentParentIsANoOp)
{
  const auto hierarchy = makeHierarchy();

  const auto secondChild = std::make_shared<Object>("Second Child");
  secondChild->setParent(hierarchy.parent);
  hierarchy.objectManager->addObject(secondChild);

  reparent(hierarchy, hierarchy.child, hierarchy.parent);

  // Re-applying the parent an object already has would otherwise move it to the end of the siblings.
  ASSERT_EQ(hierarchy.parent->getChildren().size(), 2u);
  EXPECT_EQ(hierarchy.parent->getChildren().front(), hierarchy.child);
  EXPECT_EQ(hierarchy.parent->getChildren().back(), secondChild);
}

TEST(ObjectHierarchy, ReparentOntoAChainExactlyAtTheDepthLimitIsAllowed)
{
  const auto hierarchy = makeHierarchy();

  // The chain's deepest node sits maxObjectDepth - 1 levels below its own root, so dropping a leaf onto
  // it lands the leaf at exactly maxObjectDepth.
  const auto deepParent = chainOfDepth(hierarchy, maxObjectDepth - 1);
  const auto leaf = addObject(hierarchy, "Leaf");

  reparent(hierarchy, leaf, deepParent);

  EXPECT_EQ(leaf->getParent(), deepParent);
}

TEST(ObjectHierarchy, ReparentOntoAChainOneLevelPastTheDepthLimitIsRejected)
{
  const auto hierarchy = makeHierarchy();

  // One level deeper than the allowed case above - the positive control proving this shape of edit
  // applies at all, so the rejection below is the depth check firing and not some other defect.
  const auto deepParent = chainOfDepth(hierarchy, maxObjectDepth);
  const auto leaf = addObject(hierarchy, "Leaf");

  reparent(hierarchy, leaf, deepParent);

  EXPECT_EQ(leaf->getParent(), nullptr);
}
