#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "Replication.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"

#include <nlohmann/json.hpp>
#include <memory>

namespace {
  struct Hierarchy {
    std::shared_ptr<ComponentRegistry> componentRegistry = std::make_shared<ComponentRegistry>();
    std::unique_ptr<ObjectManager> objectManager;
    std::shared_ptr<Object> grandparent;
    std::shared_ptr<Object> parent;
    std::shared_ptr<Object> child;
  };

  // grandparent -> parent -> child, built the way deserialization builds one.
  Hierarchy makeHierarchy()
  {
    Hierarchy hierarchy;
    registerDataComponents(*hierarchy.componentRegistry);
    hierarchy.objectManager = std::make_unique<ObjectManager>(hierarchy.componentRegistry);

    hierarchy.grandparent = std::make_shared<Object>("Grandparent");
    hierarchy.objectManager->addObject(hierarchy.grandparent);

    hierarchy.parent = std::make_shared<Object>("Parent");
    hierarchy.parent->setParent(hierarchy.grandparent);
    hierarchy.objectManager->addObject(hierarchy.parent);

    hierarchy.child = std::make_shared<Object>("Child");
    hierarchy.child->setParent(hierarchy.parent);
    hierarchy.objectManager->addObject(hierarchy.child);

    return hierarchy;
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
}

TEST(ObjectHierarchy, ReparentOntoOwnDescendantIsRejected)
{
  const auto hierarchy = makeHierarchy();

  const auto childUUID = hierarchy.child->getUUID();
  replication::applySceneEdit(*hierarchy.objectManager,
                              replication::buildReparentObject(hierarchy.parent->getUUID(), &childUUID));

  EXPECT_EQ(hierarchy.parent->getParent(), hierarchy.grandparent);
  EXPECT_EQ(hierarchy.child->getParent(), hierarchy.parent);
  EXPECT_TRUE(hierarchy.child->getChildren().empty());
}

TEST(ObjectHierarchy, ReparentOntoSelfIsRejected)
{
  const auto hierarchy = makeHierarchy();

  const auto parentUUID = hierarchy.parent->getUUID();
  replication::applySceneEdit(*hierarchy.objectManager,
                              replication::buildReparentObject(parentUUID, &parentUUID));

  EXPECT_EQ(hierarchy.parent->getParent(), hierarchy.grandparent);
  EXPECT_EQ(hierarchy.parent->getChildren().size(), 1u);
}

TEST(ObjectHierarchy, ReparentOntoAnAncestorIsAllowed)
{
  const auto hierarchy = makeHierarchy();

  const auto grandparentUUID = hierarchy.grandparent->getUUID();
  replication::applySceneEdit(*hierarchy.objectManager,
                              replication::buildReparentObject(hierarchy.child->getUUID(), &grandparentUUID));

  EXPECT_EQ(hierarchy.child->getParent(), hierarchy.grandparent);
  EXPECT_TRUE(hierarchy.parent->getChildren().empty());
  EXPECT_EQ(hierarchy.grandparent->getChildren().size(), 2u);
}
