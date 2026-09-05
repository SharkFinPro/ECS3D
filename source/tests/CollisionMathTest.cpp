#include <gtest/gtest.h>

#include "TestPrinters.h"
#include "collisions/Simplex.h"
#include "collisions/Support.h"
#include "objects/Object.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/SphereCollider.h"

#include <glm/vec3.hpp>
#include <memory>
#include <utility>

namespace {
  // The direction is deliberately not the point: identical fields would hide a Simplex that copied or
  // returned the wrong one of the two.
  SupportVertex vertex(const glm::vec3& point)
  {
    return { point, -point };
  }

  void expectNear(const glm::vec3& actual, const glm::vec3& expected)
  {
    constexpr float tolerance = 1e-5f;

    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
  }

  // A collider needs a Transform on the same object to place its geometry; nothing else about the scene
  // matters to the support functions. The returned Object is the collider's lifetime anchor - it holds
  // the Transform the collider keeps a weak pointer to, and the raw owner pointer behind it.
  template <typename T>
  std::pair<std::shared_ptr<Object>, std::shared_ptr<T>> makeCollider(const glm::vec3& position,
                                                                      const glm::vec3& scale)
  {
    auto object = std::make_shared<Object>("Collider");
    const auto transform = object->getComponent<Transform>(ComponentType::transform);
    transform->setPosition(position);
    transform->setScale(scale);

    auto collider = std::make_shared<T>();
    object->addComponent(collider);

    return { object, collider };
  }
}

TEST(Simplex, AddVertexPrependsAndShifts)
{
  Simplex simplex;

  EXPECT_EQ(simplex.size(), 0u);

  simplex.addVertex(vertex({ 1, 0, 0 }));
  simplex.addVertex(vertex({ 0, 1, 0 }));
  simplex.addVertex(vertex({ 0, 0, 1 }));
  simplex.addVertex(vertex({ -1, -1, -1 }));

  EXPECT_EQ(simplex.size(), 4u);
  EXPECT_EQ(simplex.getA(), glm::vec3(-1, -1, -1));
  EXPECT_EQ(simplex.getB(), glm::vec3(0, 0, 1));
  EXPECT_EQ(simplex.getC(), glm::vec3(0, 1, 0));
  EXPECT_EQ(simplex.getD(), glm::vec3(1, 0, 0));
}

TEST(Simplex, CarriesTheSearchDirectionAlongsideTheVertex)
{
  Simplex simplex;

  simplex.addVertex({ { 1, 2, 3 }, { 0, 1, 0 } });

  EXPECT_EQ(simplex.getSupportA().vertex, glm::vec3(1, 2, 3));
  EXPECT_EQ(simplex.getSupportA().direction, glm::vec3(0, 1, 0));
}

TEST(Simplex, RemoveBClosesTheGapFromTheEnd)
{
  Simplex simplex;

  simplex.addVertex(vertex({ 1, 0, 0 }));
  simplex.addVertex(vertex({ 0, 1, 0 }));
  simplex.addVertex(vertex({ 0, 0, 1 }));
  simplex.addVertex(vertex({ -1, -1, -1 }));

  simplex.removeB();

  EXPECT_EQ(simplex.size(), 3u);
  EXPECT_EQ(simplex.getA(), glm::vec3(-1, -1, -1));
  EXPECT_EQ(simplex.getB(), glm::vec3(0, 1, 0));
  EXPECT_EQ(simplex.getC(), glm::vec3(1, 0, 0));

  // Each slot carries its whole SupportVertex through the shift, not just the point.
  EXPECT_EQ(simplex.getSupportB().direction, glm::vec3(0, -1, 0));
  EXPECT_EQ(simplex.getSupportC().direction, glm::vec3(-1, 0, 0));
}

TEST(Simplex, RemoveCKeepsTheVerticesBelowIt)
{
  Simplex simplex;

  simplex.addVertex(vertex({ 1, 0, 0 }));
  simplex.addVertex(vertex({ 0, 1, 0 }));
  simplex.addVertex(vertex({ 0, 0, 1 }));
  simplex.addVertex(vertex({ -1, -1, -1 }));

  simplex.removeC();

  EXPECT_EQ(simplex.size(), 3u);
  EXPECT_EQ(simplex.getA(), glm::vec3(-1, -1, -1));
  EXPECT_EQ(simplex.getB(), glm::vec3(0, 0, 1));
  EXPECT_EQ(simplex.getC(), glm::vec3(1, 0, 0));
}

TEST(Simplex, RemoveDDropsOnlyTheLastVertex)
{
  Simplex simplex;

  simplex.addVertex(vertex({ 1, 0, 0 }));
  simplex.addVertex(vertex({ 0, 1, 0 }));
  simplex.addVertex(vertex({ 0, 0, 1 }));
  simplex.addVertex(vertex({ -1, -1, -1 }));

  simplex.removeD();

  EXPECT_EQ(simplex.size(), 3u);
  EXPECT_EQ(simplex.getA(), glm::vec3(-1, -1, -1));
  EXPECT_EQ(simplex.getB(), glm::vec3(0, 0, 1));
  EXPECT_EQ(simplex.getC(), glm::vec3(0, 1, 0));
}

TEST(Simplex, SameDirectionIsAStrictlyPositiveDot)
{
  EXPECT_TRUE(sameDirection({ 1, 0, 0 }, { 2, 1, 0 }));
  EXPECT_FALSE(sameDirection({ 1, 0, 0 }, { -1, 0, 0 }));
  EXPECT_FALSE(sameDirection({ 1, 0, 0 }, { 0, 1, 0 }));
}

TEST(ColliderSupport, BoxReturnsTheCornerFurthestAlongTheDirection)
{
  const auto [object, box] = makeCollider<BoxCollider>({ 0, 0, 0 }, { 1, 1, 1 });

  EXPECT_EQ(box->findFurthestPoint({ 1.0f, 0.5f, 0.25f }), glm::vec3(1, 1, 1));
  EXPECT_EQ(box->findFurthestPoint({ -1.0f, -0.5f, -0.25f }), glm::vec3(-1, -1, -1));
  EXPECT_EQ(box->findFurthestPoint({ -1.0f, 0.5f, -0.25f }), glm::vec3(-1, 1, -1));
}

TEST(ColliderSupport, BoxAccountsForTheTransformPositionAndScale)
{
  const auto [object, box] = makeCollider<BoxCollider>({ 10, 0, 0 }, { 2, 3, 4 });

  EXPECT_EQ(box->findFurthestPoint({ 1.0f, 0.5f, 0.25f }), glm::vec3(12, 3, 4));
  EXPECT_EQ(box->findFurthestPoint({ -1.0f, -0.5f, -0.25f }), glm::vec3(8, -3, -4));
}

TEST(ColliderSupport, BoxRotatesTheMeshBeforeMeasuringIt)
{
  const auto [object, box] = makeCollider<BoxCollider>({ 0, 0, 0 }, { 1, 2, 1 });
  const auto transform = object->getComponent<Transform>(ComponentType::transform);
  transform->setRotation({ 0, 0, 90 });

  // A quarter turn about z sends the y extent onto x, so the winning corner is the one that was at
  // y = -2. This is the only assertion that pins the Rz * Ry * Rx composition order.
  expectNear(box->findFurthestPoint({ 1.0f, 0.5f, 0.25f }), glm::vec3(2, 1, 1));
}

TEST(ColliderSupport, BoxMultipliesItsOwnScaleAndAddsItsOwnOffset)
{
  const auto [object, box] = makeCollider<BoxCollider>({ 1, 0, 0 }, { 3, 1, 1 });
  box->setPosition({ 10, 0, 0 });
  box->setScale({ 2, 1, 1 });

  // The collider's scale multiplies the transform's while its position adds - an asymmetry worth
  // pinning, since the two read alike at the call site.
  EXPECT_EQ(box->findFurthestPoint({ 1.0f, 0.5f, 0.25f }), glm::vec3(17, 1, 1));
}

TEST(ColliderSupport, SphereReturnsThePointOnTheSurface)
{
  const auto [object, sphere] = makeCollider<SphereCollider>({ 2, 0, 0 }, { 1, 1, 1 });

  EXPECT_EQ(sphere->findFurthestPoint({ 1, 0, 0 }), glm::vec3(3, 0, 0));
  EXPECT_EQ(sphere->findFurthestPoint({ 0, -1, 0 }), glm::vec3(2, -1, 0));
}

TEST(ColliderSupport, SphereScalesItsRadiusByTheLargestTransformAxis)
{
  const auto [object, sphere] = makeCollider<SphereCollider>({ 0, 0, 0 }, { 2, 5, 3 });

  EXPECT_EQ(sphere->findFurthestPoint({ 1, 0, 0 }), glm::vec3(5, 0, 0));
}

TEST(ColliderSupport, GetSupportIsTheMinkowskiDifferenceOfTheTwoSupports)
{
  const auto [firstObject, first] = makeCollider<BoxCollider>({ 0, 0, 0 }, { 1, 1, 1 });
  const auto [secondObject, second] = makeCollider<BoxCollider>({ 5, 0, 0 }, { 1, 1, 1 });

  const glm::vec3 direction{ 1.0f, 0.5f, 0.25f };
  const std::shared_ptr<Collider> other = second;

  // (1, 1, 1) on the first box minus (4, -1, -1) on the second.
  EXPECT_EQ(getSupport(first.get(), other, direction), glm::vec3(-3, 2, 2));
}
