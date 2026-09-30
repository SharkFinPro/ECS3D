#include <gtest/gtest.h>

#include "objects/Object.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/Collider.h"
#include "objects/components/collisions/SphereCollider.h"

#include <glm/glm.hpp>
#include <glm/vec3.hpp>
#include <memory>
#include <random>

namespace {
  // What getBoundingBox produced before it had per-shape overrides: one support query per axis direction.
  BoundingBox boundsFromSupportQueries(Collider& collider)
  {
    BoundingBox box;
    box.minX = collider.findFurthestPoint({ -1, 0, 0 }).x;
    box.maxX = collider.findFurthestPoint({ 1, 0, 0 }).x;
    box.minY = collider.findFurthestPoint({ 0, -1, 0 }).y;
    box.maxY = collider.findFurthestPoint({ 0, 1, 0 }).y;
    box.minZ = collider.findFurthestPoint({ 0, 0, -1 }).z;
    box.maxZ = collider.findFurthestPoint({ 0, 0, 1 }).z;

    return box;
  }

  void expectSameBounds(Collider& collider)
  {
    const auto expected = boundsFromSupportQueries(collider);
    const auto& actual = collider.getBoundingBox();

    EXPECT_EQ(expected.minX, actual.minX);
    EXPECT_EQ(expected.maxX, actual.maxX);
    EXPECT_EQ(expected.minY, actual.minY);
    EXPECT_EQ(expected.maxY, actual.maxY);
    EXPECT_EQ(expected.minZ, actual.minZ);
    EXPECT_EQ(expected.maxZ, actual.maxZ);
  }

  struct Randoms {
    std::mt19937 random;
    std::uniform_real_distribution<float> position{ -20.0f, 20.0f };
    std::uniform_real_distribution<float> scale{ 0.25f, 4.0f };
    std::uniform_real_distribution<float> angle{ -180.0f, 180.0f };

    explicit Randoms(const unsigned seed) : random(seed) {}

    glm::vec3 nextPosition() { return { position(random), position(random), position(random) }; }
    glm::vec3 nextScale() { return { scale(random), scale(random), scale(random) }; }
    glm::vec3 nextAngles() { return { angle(random), angle(random), angle(random) }; }
  };

  void placeRandomly(const std::shared_ptr<Object>& object, Randoms& randoms)
  {
    const auto transform = object->getComponent<Transform>(ComponentType::transform);
    transform->setPosition(randoms.nextPosition());
    transform->setScale(randoms.nextScale());
    transform->setRotation(randoms.nextAngles());
  }

  std::shared_ptr<BoxCollider> addRandomBox(const std::shared_ptr<Object>& object, Randoms& randoms)
  {
    auto box = std::make_shared<BoxCollider>();
    object->addComponent(box);
    box->setPosition(randoms.nextPosition() * 0.1f);
    box->setScale(randoms.nextScale());
    box->setRotation(randoms.nextAngles());

    return box;
  }

  std::shared_ptr<SphereCollider> addRandomSphere(const std::shared_ptr<Object>& object, Randoms& randoms)
  {
    auto sphere = std::make_shared<SphereCollider>();
    object->addComponent(sphere);
    sphere->setPosition(randoms.nextPosition() * 0.1f);
    sphere->setRadius(randoms.scale(randoms.random));

    return sphere;
  }
}

TEST(ColliderBounds, ABoxMatchesSixSupportQueries)
{
  Randoms randoms(11);

  for (int i = 0; i < 40; ++i)
  {
    const auto object = std::make_shared<Object>("Box");
    placeRandomly(object, randoms);
    const auto box = addRandomBox(object, randoms);

    expectSameBounds(*box);
  }
}

TEST(ColliderBounds, ASphereMatchesSixSupportQueries)
{
  Randoms randoms(12);

  for (int i = 0; i < 40; ++i)
  {
    const auto object = std::make_shared<Object>("Sphere");
    placeRandomly(object, randoms);
    const auto sphere = addRandomSphere(object, randoms);

    expectSameBounds(*sphere);
  }
}

TEST(ColliderBounds, AChildUnderARotatedAndScaledParentMatchesSixSupportQueries)
{
  Randoms randoms(13);

  for (int i = 0; i < 40; ++i)
  {
    const auto parent = std::make_shared<Object>("Parent");
    placeRandomly(parent, randoms);

    const auto child = std::make_shared<Object>("Child");
    child->setParent(parent);
    placeRandomly(child, randoms);

    if (i % 2 == 0)
    {
      expectSameBounds(*addRandomBox(child, randoms));
    }
    else
    {
      expectSameBounds(*addRandomSphere(child, randoms));
    }
  }
}

TEST(ColliderBounds, ABoundIsRecomputedAfterTheTransformMoves)
{
  Randoms randoms(14);

  const auto object = std::make_shared<Object>("Box");
  placeRandomly(object, randoms);
  const auto box = addRandomBox(object, randoms);

  expectSameBounds(*box);

  object->getComponent<Transform>(ComponentType::transform)->setPosition(randoms.nextPosition());

  expectSameBounds(*box);
}
