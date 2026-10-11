#include <gtest/gtest.h>

#include "TestPrinters.h"
#include "collisions/ContactManifold.h"
#include "collisions/NarrowPhase.h"
#include "objects/Object.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/ConvexHull.h"
#include "objects/components/collisions/ConvexMeshCollider.h"
#include "objects/components/collisions/SphereCollider.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace {
  constexpr float tolerance = 1e-3f;

  template <typename T>
  struct Body
  {
    std::shared_ptr<Object> object;
    std::shared_ptr<T> collider;
  };

  // The returned Object anchors the Transform the collider points at.
  template <typename T>
  Body<T> make(const glm::vec3& position, const glm::vec3& scale = glm::vec3(1),
               const glm::vec3& rotation = glm::vec3(0))
  {
    auto object = std::make_shared<Object>("Collider");
    const auto transform = object->getComponent<Transform>(ComponentType::transform);
    transform->setPosition(position);
    transform->setScale(scale);
    transform->setRotation(rotation);

    auto collider = std::make_shared<T>();
    object->addComponent(collider);

    return { object, collider };
  }

  Body<ConvexMeshCollider> makeTetrahedron(const glm::vec3& position)
  {
    auto body = make<ConvexMeshCollider>(position);
    const std::vector<glm::vec3> points{ { -1, -1, -1 }, { 1, -1, -1 }, { 0, -1, 1 }, { 0, 1, 0 } };
    EXPECT_TRUE(body.collider->setHull(*buildConvexHull(points)));

    return body;
  }

  void expectNear(const char* what, const glm::vec3& actual, const glm::vec3& expected)
  {
    SCOPED_TRACE(::testing::Message()
                 << what
                 << ": expected (" << expected.x << ", " << expected.y << ", " << expected.z << ")"
                 << ", got (" << actual.x << ", " << actual.y << ", " << actual.z << ")");

    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
  }

  bool hasPointNear(std::span<const glm::vec3> points, const glm::vec3& expected)
  {
    return std::ranges::any_of(points, [&expected](const glm::vec3& point) {
      return glm::distance(point, expected) < tolerance;
    });
  }

  void expectPointsInsideSquare(std::span<const glm::vec3> points, const float xMin, const float xMax,
                                const float zMin, const float zMax)
  {
    for (const auto& point : points)
    {
      EXPECT_GE(point.x, xMin - tolerance);
      EXPECT_LE(point.x, xMax + tolerance);
      EXPECT_GE(point.z, zMin - tolerance);
      EXPECT_LE(point.z, zMax + tolerance);
    }
  }

  std::vector<glm::vec3> squareAt(const float y, const float xMin, const float xMax, const float z)
  {
    return { { xMin, y, -z }, { xMax, y, -z }, { xMax, y, z }, { xMin, y, z } };
  }

  float smallestSeparation(const std::vector<glm::vec3>& points)
  {
    float smallest = 1e9f;

    for (size_t i = 0; i < points.size(); ++i)
    {
      for (size_t j = i + 1; j < points.size(); ++j)
      {
        smallest = std::min(smallest, glm::distance(points[i], points[j]));
      }
    }

    return smallest;
  }
}

TEST(ContactManifold, TouchingFeatureIsTheCornerEdgeOrFaceNearestTheDirection)
{
  const auto cube = makeUnitCubeHull();

  EXPECT_EQ(collisions::touchingFeature(cube.vertices, { 0, 1, 0 }).size(), 4u);
  EXPECT_EQ(collisions::touchingFeature(cube.vertices, { 1, 1, 0 }).size(), 2u);
  EXPECT_EQ(collisions::touchingFeature(cube.vertices, { 1, 1, 1 }).size(), 1u);

  for (const auto& vertex : collisions::touchingFeature(cube.vertices, { 0, 1, 0 }))
  {
    EXPECT_FLOAT_EQ(vertex.y, 1.0f);
  }

  EXPECT_TRUE(collisions::touchingFeature({}, { 0, 1, 0 }).empty());
}

TEST(ContactManifold, ClipsTheIncidentFaceToTheOverlapOfTheTwoFootprints)
{
  // The lower body's top face spans x -1..1; the upper body's bottom face spans x 0..2 and sits 0.1 lower.
  const auto lowerTop = squareAt(1.0f, -1.0f, 1.0f, 1.0f);
  const auto upperBottom = squareAt(0.9f, 0.0f, 2.0f, 1.0f);

  const auto manifold = collisions::clipManifold(lowerTop, upperBottom, { 0, 1, 0 }, 4);

  ASSERT_EQ(manifold.size(), 4u);
  expectPointsInsideSquare(manifold, 0.0f, 1.0f, -1.0f, 1.0f);

  for (const float x : { 0.0f, 1.0f })
  {
    for (const float z : { -1.0f, 1.0f })
    {
      EXPECT_TRUE(hasPointNear(manifold, { x, 1.0f, z })) << x << ", " << z;
    }
  }

  // Swapping the sides makes the other face the reference: the same footprint, on that face's plane.
  const auto swapped = collisions::clipManifold(upperBottom, lowerTop, { 0, -1, 0 }, 4);

  ASSERT_EQ(swapped.size(), 4u);
  expectPointsInsideSquare(swapped, 0.0f, 1.0f, -1.0f, 1.0f);

  for (const auto& point : swapped)
  {
    EXPECT_NEAR(point.y, 0.9f, tolerance);
  }
}

TEST(ContactManifold, FacesThatDoNotOverlapFallBackToTheIncidentFeatureOnTheReferencePlane)
{
  const auto lowerTop = squareAt(1.0f, -1.0f, 1.0f, 1.0f);
  const auto farAway = squareAt(0.9f, 5.0f, 7.0f, 1.0f);

  const auto manifold = collisions::clipManifold(lowerTop, farAway, { 0, 1, 0 }, 4);

  EXPECT_FALSE(manifold.empty());
}

TEST(ContactManifold, ASinglePointOnAFaceLandsOnThatFace)
{
  const auto faceTop = squareAt(-0.1f, -1.0f, 1.0f, 1.0f);
  const std::vector<glm::vec3> corner{ { 0.2f, 0.0f, 0.3f } };

  const auto manifold = collisions::clipManifold(corner, faceTop, { 0, -1, 0 }, 4);

  ASSERT_EQ(manifold.size(), 1u);
  expectNear("point", manifold[0], { 0.2f, -0.1f, 0.3f });
}

TEST(ContactManifold, TwoCrossedEdgesMeetAtOnePointMidwayBetweenThem)
{
  const std::vector<glm::vec3> upperEdge{ { -1, 0, 0 }, { 1, 0, 0 } };
  const std::vector<glm::vec3> lowerEdge{ { 0, -0.1f, -1 }, { 0, -0.1f, 1 } };

  const auto manifold = collisions::clipManifold(upperEdge, lowerEdge, { 0, -1, 0 }, 4);

  ASSERT_EQ(manifold.size(), 1u);
  expectNear("point", manifold[0], { 0, -0.05f, 0 });
}

TEST(ContactManifold, MoreThanTheCapKeepsTheSpreadOutPointsDeterministically)
{
  std::vector<glm::vec3> octagon;
  for (int i = 0; i < 8; ++i)
  {
    const float angle = static_cast<float>(i) * 0.785398163f;
    octagon.emplace_back(std::cos(angle), 0.9f, std::sin(angle));
  }

  const auto bigSquare = squareAt(1.0f, -2.0f, 2.0f, 2.0f);

  const auto manifold = collisions::clipManifold(bigSquare, octagon, { 0, 1, 0 }, 4);

  ASSERT_EQ(manifold.size(), 4u);

  // Adjacent octagon corners are 0.77 apart; four spread-out ones are at least a diagonal of the square
  // through them (1.41) from each other.
  EXPECT_GT(smallestSeparation(manifold), 1.2f);

  const auto again = collisions::clipManifold(bigSquare, octagon, { 0, 1, 0 }, 4);
  ASSERT_EQ(again.size(), manifold.size());
  for (size_t i = 0; i < manifold.size(); ++i)
  {
    EXPECT_EQ(manifold[i], again[i]);
  }

  // The control: with room for all of them, all of them are kept.
  EXPECT_EQ(collisions::clipManifold(bigSquare, octagon, { 0, 1, 0 }, 8).size(), 8u);
}

TEST(ConvexMeshNarrowPhase, ReportsNoContactForSeparatedPairsAndContactOnceTheyTouch)
{
  const auto hull = make<ConvexMeshCollider>({ 0, 0, 0 });

  auto box = make<BoxCollider>({ 0, 3, 0 });
  auto otherHull = make<ConvexMeshCollider>({ 0, 3, 0 });
  auto sphere = make<SphereCollider>({ 0, 3, 0 });

  EXPECT_FALSE(collisions::findContact(*hull.collider, *box.collider).has_value());
  EXPECT_FALSE(collisions::findContact(*hull.collider, *otherHull.collider).has_value());
  EXPECT_FALSE(collisions::findContact(*hull.collider, *sphere.collider).has_value());
  EXPECT_FALSE(collisions::intersects(*hull.collider, *box.collider));
  EXPECT_FALSE(collisions::intersects(*hull.collider, *otherHull.collider));
  EXPECT_FALSE(collisions::intersects(*hull.collider, *sphere.collider));

  for (const auto& object : { box.object, otherHull.object, sphere.object })
  {
    object->getComponent<Transform>(ComponentType::transform)->setPosition({ 0, 1.9f, 0 });
  }

  EXPECT_TRUE(collisions::findContact(*hull.collider, *box.collider).has_value());
  EXPECT_TRUE(collisions::findContact(*hull.collider, *otherHull.collider).has_value());
  EXPECT_TRUE(collisions::findContact(*hull.collider, *sphere.collider).has_value());
  EXPECT_TRUE(collisions::intersects(*hull.collider, *box.collider));
  EXPECT_TRUE(collisions::intersects(*hull.collider, *otherHull.collider));
  EXPECT_TRUE(collisions::intersects(*hull.collider, *sphere.collider));
}

TEST(ConvexMeshNarrowPhase, AHullRestingFlatOnABoxGetsAFourPointManifoldInsideBothFootprints)
{
  const auto hull = make<ConvexMeshCollider>({ 0, 0, 0 });
  const auto box = make<BoxCollider>({ 0, 1.9f, 0 });

  const auto contact = collisions::findContact(*hull.collider, *box.collider);
  ASSERT_TRUE(contact.has_value());

  EXPECT_NEAR(contact->depth(), 0.1f, tolerance);

  ASSERT_EQ(contact->pointCount, 4);
  expectPointsInsideSquare(contact->contactPoints(), -1.0f, 1.0f, -1.0f, 1.0f);

  for (const float x : { -1.0f, 1.0f })
  {
    for (const float z : { -1.0f, 1.0f })
    {
      EXPECT_TRUE(hasPointNear(contact->contactPoints(), { x, 1.0f, z })) << x << ", " << z;
    }
  }

  expectNear("point", contact->point, { 0, 1.0f, 0 });
}

TEST(ConvexMeshNarrowPhase, TheNormalPointsAwayFromTheOtherColliderWhicheverSideTheHullIsOn)
{
  const auto hull = make<ConvexMeshCollider>({ 0, 0, 0 });
  const auto box = make<BoxCollider>({ 0, 1.9f, 0 });

  const auto hullFirst = collisions::findContact(*hull.collider, *box.collider);
  const auto boxFirst = collisions::findContact(*box.collider, *hull.collider);

  ASSERT_TRUE(hullFirst.has_value());
  ASSERT_TRUE(boxFirst.has_value());

  // Each moves its own first collider clear: the hull down, the box up.
  expectNear("hull first", hullFirst->normal(), { 0, -1, 0 });
  expectNear("box first", boxFirst->normal(), { 0, 1, 0 });
  EXPECT_NEAR(boxFirst->depth(), 0.1f, tolerance);

  ASSERT_EQ(boxFirst->pointCount, 4);
  expectPointsInsideSquare(boxFirst->contactPoints(), -1.0f, 1.0f, -1.0f, 1.0f);
}

TEST(ConvexMeshNarrowPhase, AShiftedHullTouchesOnlyTheOverlappingPartOfTheFace)
{
  const auto hull = make<ConvexMeshCollider>({ 0, 0, 0 });
  const auto box = make<BoxCollider>({ 1.0f, 1.9f, 0 });

  const auto contact = collisions::findContact(*hull.collider, *box.collider);
  ASSERT_TRUE(contact.has_value());

  // The box's face spans x 0..2 and the hull's -1..1, so the clipped manifold spans x 0..1.
  ASSERT_EQ(contact->pointCount, 4);
  expectPointsInsideSquare(contact->contactPoints(), 0.0f, 1.0f, -1.0f, 1.0f);
  EXPECT_TRUE(hasPointNear(contact->contactPoints(), { 0.0f, 1.0f, -1.0f }));
  EXPECT_TRUE(hasPointNear(contact->contactPoints(), { 1.0f, 1.0f, 1.0f }));
}

TEST(ConvexMeshNarrowPhase, TwoHullsRestingFaceToFaceGetAFourPointManifold)
{
  const auto lower = make<ConvexMeshCollider>({ 0, 0, 0 });
  const auto upper = make<ConvexMeshCollider>({ 1.0f, 1.9f, 0 });

  const auto contact = collisions::findContact(*lower.collider, *upper.collider);
  ASSERT_TRUE(contact.has_value());

  EXPECT_NEAR(contact->depth(), 0.1f, tolerance);
  expectNear("normal", contact->normal(), { 0, -1, 0 });

  ASSERT_EQ(contact->pointCount, 4);
  expectPointsInsideSquare(contact->contactPoints(), 0.0f, 1.0f, -1.0f, 1.0f);

  const auto reversed = collisions::findContact(*upper.collider, *lower.collider);
  ASSERT_TRUE(reversed.has_value());
  expectNear("reversed normal", reversed->normal(), { 0, 1, 0 });
  ASSERT_EQ(reversed->pointCount, 4);
  expectPointsInsideSquare(reversed->contactPoints(), 0.0f, 1.0f, -1.0f, 1.0f);
}

TEST(ConvexMeshNarrowPhase, AHullOnItsEdgeOnABoxFaceGetsATwoPointManifold)
{
  // A box ground whose top is y = 1, and a hull cube turned 45 degrees about z so it stands on an edge
  // running along z, 0.5 long on each side of the middle. The lowest edge sits 0.05 below the ground top.
  const auto ground = make<BoxCollider>({ 0, 0, 0 });
  const auto hull = make<ConvexMeshCollider>({ 0, 1.0f + std::sqrt(2.0f) - 0.05f, 0 }, { 1, 1, 0.5f },
                                             { 0, 0, 45 });

  const auto contact = collisions::findContact(*ground.collider, *hull.collider);
  ASSERT_TRUE(contact.has_value());

  EXPECT_NEAR(contact->depth(), 0.05f, tolerance);
  expectNear("normal", contact->normal(), { 0, -1, 0 });

  ASSERT_EQ(contact->pointCount, 2);
  EXPECT_TRUE(hasPointNear(contact->contactPoints(), { 0, 1.0f, -0.5f }));
  EXPECT_TRUE(hasPointNear(contact->contactPoints(), { 0, 1.0f, 0.5f }));
}

TEST(ConvexMeshNarrowPhase, ATetrahedronOnItsBaseTouchesTheBoxAcrossTheBase)
{
  const auto ground = make<BoxCollider>({ 0, 0, 0 });
  const auto tetrahedron = makeTetrahedron({ 0, 2.0f - 0.05f, 0 });

  const auto contact = collisions::findContact(*ground.collider, *tetrahedron.collider);
  ASSERT_TRUE(contact.has_value());

  EXPECT_NEAR(contact->depth(), 0.05f, tolerance);
  expectNear("normal", contact->normal(), { 0, -1, 0 });

  // The base triangle has three corners, fewer than the box's four, so the box is the reference.
  ASSERT_EQ(contact->pointCount, 3);
  EXPECT_TRUE(hasPointNear(contact->contactPoints(), { -1, 1.0f, -1 }));
  EXPECT_TRUE(hasPointNear(contact->contactPoints(), { 1, 1.0f, -1 }));
  EXPECT_TRUE(hasPointNear(contact->contactPoints(), { 0, 1.0f, 1 }));
}

TEST(ConvexMeshNarrowPhase, AHullAndASphereMeetAtASinglePointOnTheSphereSideOfTheOverlap)
{
  const auto hull = make<ConvexMeshCollider>({ 0, 0, 0 });
  const auto sphere = make<SphereCollider>({ 0, 1.9f, 0 });

  const auto hullFirst = collisions::findContact(*hull.collider, *sphere.collider);
  ASSERT_TRUE(hullFirst.has_value());

  EXPECT_NEAR(hullFirst->depth(), 0.1f, tolerance);
  expectNear("hull first normal", hullFirst->normal(), { 0, -1, 0 });
  EXPECT_EQ(hullFirst->pointCount, 0);
  expectNear("hull first point", hullFirst->point, { 0, 0.9f, 0 });

  const auto sphereFirst = collisions::findContact(*sphere.collider, *hull.collider);
  ASSERT_TRUE(sphereFirst.has_value());

  expectNear("sphere first normal", sphereFirst->normal(), { 0, 1, 0 });
  EXPECT_EQ(sphereFirst->pointCount, 0);
  expectNear("sphere first point", sphereFirst->point, { 0, 0.9f, 0 });
}

TEST(ConvexMeshNarrowPhase, AScaledHullTakesItsSizeFromTheTransformLikeABox)
{
  // Spans x -3..3 once scaled, so a unit box centered at 3.5 overlaps it by 0.5 - only true if the scale
  // reached the hull's world vertices.
  const auto hull = make<ConvexMeshCollider>({ 0, 0, 0 }, { 3, 1, 1 });
  const auto box = make<BoxCollider>({ 3.5f, 0, 0 });

  const auto contact = collisions::findContact(*hull.collider, *box.collider);
  ASSERT_TRUE(contact.has_value());

  EXPECT_NEAR(contact->depth(), 0.5f, tolerance);
  expectNear("normal", contact->normal(), { -1, 0, 0 });
}
