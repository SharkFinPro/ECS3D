#include <gtest/gtest.h>

#include "TestScene.h"
#include "objects/Object.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/SphereCollider.h"

#include <cmath>
#include <glm/vec3.hpp>
#include <memory>

namespace {
  using fixtures::transformOf;

  struct Chain {
    fixtures::Scene scene = fixtures::makeScene();
    std::shared_ptr<Object> parent;
    std::shared_ptr<Object> child;
    std::shared_ptr<BoxCollider> box;

    Chain()
    {
      parent = fixtures::addObject(scene, "Parent");
      child = fixtures::addChildObject(scene, "Child", parent);
      box = fixtures::addBoxCollider(child);
    }
  };

  float width(const BoundingBox& bounds)
  {
    return bounds.maxX - bounds.minX;
  }

  float centerX(const BoundingBox& bounds)
  {
    return (bounds.maxX + bounds.minX) * 0.5f;
  }
}

TEST(ColliderCache, MovingTheParentMovesAChildBoxBounds)
{
  Chain chain;
  transformOf(chain.child)->setPosition(glm::vec3(1, 0, 0));
  const float before = centerX(chain.box->getBoundingBox());
  const float supportBefore = chain.box->findFurthestPoint({1, 0, 0}).x;

  transformOf(chain.child)->setPosition(glm::vec3(2, 0, 0));
  EXPECT_NEAR(centerX(chain.box->getBoundingBox()), before + 1.0f, 1e-4f);

  transformOf(chain.parent)->setPosition(glm::vec3(5, 0, 0));

  EXPECT_NEAR(centerX(chain.box->getBoundingBox()), before + 6.0f, 1e-4f);
  EXPECT_NEAR(chain.box->findFurthestPoint({1, 0, 0}).x, supportBefore + 6.0f, 1e-4f);
}

TEST(ColliderCache, ScalingTheParentResizesAChildBoxBounds)
{
  Chain chain;
  const float before = width(chain.box->getBoundingBox());
  const float supportBefore = chain.box->findFurthestPoint({1, 0, 0}).x;

  transformOf(chain.parent)->setScale(glm::vec3(2, 2, 2));

  EXPECT_NEAR(width(chain.box->getBoundingBox()), before * 2.0f, 1e-4f);
  EXPECT_NEAR(chain.box->findFurthestPoint({1, 0, 0}).x, supportBefore * 2.0f, 1e-4f);
}

TEST(ColliderCache, RotatingTheParentTurnsAChildBoxBounds)
{
  Chain chain;
  const float before = width(chain.box->getBoundingBox());
  const float supportBefore = chain.box->findFurthestPoint({1, 0, 0}).x;

  transformOf(chain.parent)->setRotation(glm::vec3(0, 0, 45));

  const float grown = static_cast<float>(std::sqrt(2.0));
  EXPECT_NEAR(width(chain.box->getBoundingBox()), before * grown, 1e-4f);
  EXPECT_NEAR(chain.box->findFurthestPoint({1, 0, 0}).x, supportBefore * grown, 1e-4f);
}

TEST(ColliderCache, MovingAGrandparentMovesAGrandchildBoxBounds)
{
  fixtures::Scene scene;
  const auto grandparent = fixtures::addObject(scene, "Grandparent");
  const auto parent = fixtures::addChildObject(scene, "Parent", grandparent);
  const auto child = fixtures::addChildObject(scene, "Child", parent);
  const auto box = fixtures::addBoxCollider(child);

  const float before = centerX(box->getBoundingBox());
  const float supportBefore = box->findFurthestPoint({1, 0, 0}).x;

  transformOf(grandparent)->setPosition(glm::vec3(3, 0, 0));

  EXPECT_NEAR(centerX(box->getBoundingBox()), before + 3.0f, 1e-4f);
  EXPECT_NEAR(box->findFurthestPoint({1, 0, 0}).x, supportBefore + 3.0f, 1e-4f);
}

TEST(ColliderCache, MovingTheParentMovesAChildSphereBounds)
{
  fixtures::Scene scene;
  const auto parent = fixtures::addObject(scene, "Parent");
  const auto child = fixtures::addChildObject(scene, "Child", parent);
  const auto sphere = fixtures::addSphereCollider(child, 1.0f);

  const float before = centerX(sphere->getBoundingBox());

  transformOf(parent)->setPosition(glm::vec3(4, 0, 0));
  EXPECT_NEAR(centerX(sphere->getBoundingBox()), before + 4.0f, 1e-4f);

  const float movedWidth = width(sphere->getBoundingBox());
  transformOf(parent)->setScale(glm::vec3(3, 3, 3));
  EXPECT_NEAR(width(sphere->getBoundingBox()), movedWidth * 3.0f, 1e-4f);
}

TEST(ColliderCache, ReparentingUnderAChainWithAnOlderStampRefreshesTheCache)
{
  const auto quiet = std::make_shared<Object>("Quiet");
  const auto busy = std::make_shared<Object>("Busy");
  const auto child = std::make_shared<Object>("Child");

  transformOf(quiet)->setPosition(glm::vec3(10, 0, 0));

  for (int i = 0; i < 5; ++i)
  {
    transformOf(busy)->setPosition(glm::vec3(static_cast<float>(i), 0, 0));
  }

  const auto box = std::make_shared<BoxCollider>();
  child->addComponent(box);
  child->setParent(busy);

  const float underBusy = centerX(box->getBoundingBox());
  const auto busyWorldID = transformOf(child)->getWorldUpdateID();
  EXPECT_NEAR(underBusy, 4.0f, 1e-4f);

  child->setParent(quiet);

  EXPECT_NE(transformOf(child)->getWorldUpdateID(), busyWorldID);
  EXPECT_NEAR(centerX(box->getBoundingBox()), 10.0f, 1e-4f);
  EXPECT_NEAR(box->findFurthestPoint({1, 0, 0}).x, box->getBoundingBox().maxX, 1e-4f);
}

TEST(ColliderCache, ReparentingNeverRepeatsAWorldUpdateID)
{
  const auto first = std::make_shared<Object>("First");
  const auto second = std::make_shared<Object>("Second");
  const auto child = std::make_shared<Object>("Child");

  transformOf(first)->setPosition(glm::vec3(1, 0, 0));
  transformOf(first)->setPosition(glm::vec3(2, 0, 0));
  transformOf(second)->setPosition(glm::vec3(1, 0, 0));

  child->setParent(first);
  const auto underFirst = transformOf(child)->getWorldUpdateID();
  child->setParent(second);
  const auto underSecond = transformOf(child)->getWorldUpdateID();
  child->setParent(first);

  EXPECT_NE(underFirst, underSecond);
  EXPECT_GT(underSecond, underFirst);
  EXPECT_GT(transformOf(child)->getWorldUpdateID(), underSecond);
}

TEST(ColliderCache, NothingChangingKeepsTheCache)
{
  Chain chain;
  transformOf(chain.parent)->setPosition(glm::vec3(1, 0, 0));

  const BoundingBox first = chain.box->getBoundingBox();
  const auto worldID = transformOf(chain.child)->getWorldUpdateID();

  const BoundingBox& second = chain.box->getBoundingBox();

  EXPECT_EQ(second.lastUpdateID, first.lastUpdateID);
  EXPECT_EQ(second.lastUpdateID, worldID);
  EXPECT_EQ(transformOf(chain.child)->getWorldUpdateID(), worldID);
  EXPECT_FLOAT_EQ(second.minX, first.minX);
  EXPECT_FLOAT_EQ(second.maxX, first.maxX);

  transformOf(chain.parent)->setPosition(glm::vec3(2, 0, 0));

  EXPECT_NE(chain.box->getBoundingBox().lastUpdateID, first.lastUpdateID);
}

TEST(ColliderCache, RemovingTheParentTransformRefreshesAChildBoxBounds)
{
  Chain chain;
  transformOf(chain.parent)->setPosition(glm::vec3(5, 0, 0));
  transformOf(chain.parent)->setPosition(glm::vec3(6, 0, 0));
  transformOf(chain.child)->setPosition(glm::vec3(1, 0, 0));

  const float offset = centerX(chain.box->getBoundingBox());
  const float supportOffset = chain.box->findFurthestPoint({1, 0, 0}).x;

  chain.parent->removeComponent(transformOf(chain.parent));

  EXPECT_NEAR(centerX(chain.box->getBoundingBox()), offset - 6.0f, 1e-4f);
  EXPECT_NEAR(chain.box->findFurthestPoint({1, 0, 0}).x, supportOffset - 6.0f, 1e-4f);

  chain.parent->addComponent(std::make_shared<Transform>(glm::vec3(2, 0, 0), glm::vec3(1), glm::vec3(0)));

  EXPECT_NEAR(centerX(chain.box->getBoundingBox()), offset - 4.0f, 1e-4f);
}

TEST(ColliderCache, RemovingATransformThroughAReferenceIntoTheComponentMapRefreshesTheChild)
{
  Chain chain;
  transformOf(chain.parent)->setPosition(glm::vec3(6, 0, 0));
  const float offset = centerX(chain.box->getBoundingBox());

  // The reference names the map entry removeComponent erases, the way Replication's removeComponent op passes it.
  const auto& held = chain.parent->getComponents().at(ComponentType::transform);
  chain.parent->removeComponent(held);

  EXPECT_FALSE(chain.parent->getComponents().contains(ComponentType::transform));
  EXPECT_NEAR(centerX(chain.box->getBoundingBox()), offset - 6.0f, 1e-4f);
}

TEST(ColliderCache, TheWorldUpdateIDIgnoresAncestorsAboveATransformlessOne)
{
  fixtures::Scene scene;
  const auto grandparent = fixtures::addObject(scene, "Grandparent");
  const auto parent = fixtures::addChildObject(scene, "Parent", grandparent);
  const auto child = fixtures::addChildObject(scene, "Child", parent);

  parent->removeComponent(transformOf(parent));
  const auto before = transformOf(child)->getWorldUpdateID();

  transformOf(grandparent)->setPosition(glm::vec3(9, 0, 0));

  EXPECT_EQ(transformOf(child)->getWorldUpdateID(), before);
  EXPECT_NEAR(transformOf(child)->getPosition().x, 0.0f, 1e-5f);

  transformOf(child)->setPosition(glm::vec3(1, 0, 0));

  EXPECT_NE(transformOf(child)->getWorldUpdateID(), before);
}
