#include <gtest/gtest.h>

#include "ConvexMeshFixtures.h"
#include "TestPrinters.h"
#include "TestScene.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/ConvexHull.h"
#include "objects/components/collisions/ConvexHullValidation.h"
#include "objects/components/collisions/ConvexMeshCollider.h"

#include <glm/vec3.hpp>
#include <array>
#include <memory>
#include <utility>

namespace {
  using convexFixtures::expectNear;
  using convexFixtures::tetrahedron;
  using fixtures::addChildObject;
  using fixtures::addObject;
  using fixtures::makeScene;
  using fixtures::transformOf;

  struct Placed
  {
    std::shared_ptr<Object> object;
    std::shared_ptr<ConvexMeshCollider> hull;
  };

  Placed placeHull(const fixtures::Scene& scene, const glm::vec3& position = glm::vec3(0.0f))
  {
    auto object = addObject(scene, "Hull", position);
    auto hull = std::make_shared<ConvexMeshCollider>();
    object->addComponent(hull);

    return { object, hull };
  }

  struct Pair
  {
    std::shared_ptr<BoxCollider> box;
    std::shared_ptr<ConvexMeshCollider> hull;
    std::shared_ptr<Object> boxObject;
    std::shared_ptr<Object> hullObject;
  };

  void configure(const std::shared_ptr<Object>& object)
  {
    const auto transform = transformOf(object);
    transform->setPosition({ 1.0f, 2.0f, 3.0f });
    transform->setRotation({ 10.0f, 20.0f, 30.0f });
    transform->setScale({ 1.5f, 2.0f, 0.75f });
  }

  void expectSamePlacement(Pair& pair)
  {
    expectNear("position", pair.hull->getPosition(), pair.box->getPosition());
    expectNear("scale", pair.hull->getScale(), pair.box->getScale());
    expectNear("rotation", pair.hull->getRotation(), pair.box->getRotation(), 1e-3f);

    const std::array<glm::vec3, 6> directions{ {
      { 0.3f, 0.5f, 0.8f }, { -0.7f, 0.2f, -0.4f }, { 0.1f, -0.9f, 0.35f },
      { -0.45f, -0.3f, 0.6f }, { 0.9f, 0.15f, -0.2f }, { -0.1f, 0.7f, 0.55f }
    } };

    for (const auto& direction : directions)
    {
      expectNear("furthest", pair.hull->findFurthestPoint(direction), pair.box->findFurthestPoint(direction),
                 1e-4f);
    }

    const auto& boxBounds = pair.box->getBoundingBox();
    const auto& hullBounds = pair.hull->getBoundingBox();
    EXPECT_NEAR(hullBounds.minX, boxBounds.minX, 1e-4f);
    EXPECT_NEAR(hullBounds.maxX, boxBounds.maxX, 1e-4f);
    EXPECT_NEAR(hullBounds.minY, boxBounds.minY, 1e-4f);
    EXPECT_NEAR(hullBounds.maxY, boxBounds.maxY, 1e-4f);
    EXPECT_NEAR(hullBounds.minZ, boxBounds.minZ, 1e-4f);
    EXPECT_NEAR(hullBounds.maxZ, boxBounds.maxZ, 1e-4f);
  }

  void applyOffsets(Pair& pair)
  {
    pair.box->setPosition({ 0.5f, -0.25f, 0.2f });
    pair.hull->setPosition({ 0.5f, -0.25f, 0.2f });
    pair.box->setRotation({ 5.0f, 15.0f, -25.0f });
    pair.hull->setRotation({ 5.0f, 15.0f, -25.0f });
    pair.box->setScale({ 1.0f, 0.5f, 2.0f });
    pair.hull->setScale({ 1.0f, 0.5f, 2.0f });
  }
}

TEST(ConvexMeshCollider, TheBoundingBoxFollowsChangesToTheColliderItself)
{
  const auto scene = makeScene();
  const auto [object, hull] = placeHull(scene);

  EXPECT_FLOAT_EQ(hull->getBoundingBox().maxX, 1.0f);

  hull->setScale({ 3.0f, 1.0f, 1.0f });
  EXPECT_FLOAT_EQ(hull->getBoundingBox().maxX, 3.0f);

  hull->setPosition({ 4.0f, 0.0f, 0.0f });
  EXPECT_FLOAT_EQ(hull->getBoundingBox().maxX, 7.0f);
  EXPECT_FLOAT_EQ(hull->getBoundingBox().minX, 1.0f);

  ASSERT_TRUE(hull->setHull(tetrahedron()));
  EXPECT_FLOAT_EQ(hull->getBoundingBox().maxY, 1.0f);
  EXPECT_FLOAT_EQ(hull->getBoundingBox().minY, -1.0f);
  EXPECT_FLOAT_EQ(hull->getBoundingBox().maxX, 4.0f + 3.0f);
}

TEST(ConvexMeshCollider, TheDefaultCubeHullPlacesItselfLikeABoxWithTheSameOffsets)
{
  const auto scene = makeScene();

  Pair pair;
  pair.boxObject = addObject(scene, "Box");
  pair.hullObject = addObject(scene, "Hull");
  pair.box = fixtures::addBoxCollider(pair.boxObject);
  pair.hull = std::make_shared<ConvexMeshCollider>();
  pair.hullObject->addComponent(pair.hull);

  configure(pair.boxObject);
  configure(pair.hullObject);
  expectSamePlacement(pair);

  applyOffsets(pair);
  expectSamePlacement(pair);
}

TEST(ConvexMeshCollider, TheDefaultCubeHullPlacesItselfLikeABoxUnderARotatedScaledParent)
{
  const auto scene = makeScene();

  const auto parent = addObject(scene, "Parent", { 4.0f, -1.0f, 2.0f });
  transformOf(parent)->setRotation({ 0.0f, 30.0f, 15.0f });
  transformOf(parent)->setScale({ 2.0f, 1.0f, 1.5f });

  Pair pair;
  pair.boxObject = addChildObject(scene, "Box", parent);
  pair.hullObject = addChildObject(scene, "Hull", parent);
  pair.box = fixtures::addBoxCollider(pair.boxObject);
  pair.hull = std::make_shared<ConvexMeshCollider>();
  pair.hullObject->addComponent(pair.hull);

  configure(pair.boxObject);
  configure(pair.hullObject);
  applyOffsets(pair);
  expectSamePlacement(pair);

  // Moving the ancestor moves both, and the cache notices.
  transformOf(parent)->setPosition({ -6.0f, 3.0f, 1.0f });
  expectSamePlacement(pair);
}
