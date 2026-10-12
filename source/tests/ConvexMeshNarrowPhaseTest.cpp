#include <gtest/gtest.h>

#include "ConvexMeshFixtures.h"
#include "TestPrinters.h"
#include "TestScene.h"
#include "collisions/NarrowPhase.h"
#include "objects/Object.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/CapsuleCollider.h"
#include "objects/components/collisions/ConvexMeshCollider.h"
#include "objects/components/collisions/SphereCollider.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <span>
#include <vector>

namespace {
  using fixtures::addObject;
  using fixtures::expectNear;
  using fixtures::transformOf;

  constexpr float tolerance = 1e-3f;

  // The default hull is the unit cube [-1,1]^3 before the transform, like a default BoxCollider.
  std::shared_ptr<ConvexMeshCollider> hullAt(const fixtures::Scene& scene, const glm::vec3& position,
                                             const glm::vec3& scale = glm::vec3(1),
                                             const glm::vec3& rotation = glm::vec3(0))
  {
    const auto object = addObject(scene, "Hull", position, scale);
    transformOf(object)->setRotation(rotation);

    const auto collider = std::make_shared<ConvexMeshCollider>();
    object->addComponent(collider);

    return collider;
  }

  std::shared_ptr<ConvexMeshCollider> tetrahedronAt(const fixtures::Scene& scene, const glm::vec3& position)
  {
    const auto collider = hullAt(scene, position);
    EXPECT_TRUE(collider->setHull(convexFixtures::tetrahedron()));

    return collider;
  }

  // The box [-1,1]^3 moved to position: its top face is at position.y + 1.
  std::shared_ptr<BoxCollider> boxAt(const fixtures::Scene& scene, const glm::vec3& position)
  {
    return fixtures::addBoxCollider(addObject(scene, "Box", position));
  }

  void moveTo(const Collider& collider, const glm::vec3& position)
  {
    transformOf(collider.getOwner())->setPosition(position);
  }

  bool hasPointNear(std::span<const glm::vec3> points, const glm::vec3& expected, const float within = tolerance)
  {
    return std::ranges::any_of(points, [&expected, within](const glm::vec3& point) {
      return glm::distance(point, expected) < within;
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
}

TEST(ConvexMeshNarrowPhase, ReportsNoContactForSeparatedPairsAndContactOnceTheyTouch)
{
  const auto scene = fixtures::makeScene();
  const auto hull = hullAt(scene, { 0, 0, 0 });

  const auto box = boxAt(scene, { 0, 3, 0 });
  const auto otherHull = hullAt(scene, { 0, 3, 0 });
  const auto sphere = fixtures::addSphereCollider(addObject(scene, "Sphere", { 0, 3, 0 }), 1.0f);

  EXPECT_FALSE(collisions::findContact(*hull, *box).has_value());
  EXPECT_FALSE(collisions::findContact(*hull, *otherHull).has_value());
  EXPECT_FALSE(collisions::findContact(*hull, *sphere).has_value());
  EXPECT_FALSE(collisions::intersects(*hull, *box));
  EXPECT_FALSE(collisions::intersects(*hull, *otherHull));
  EXPECT_FALSE(collisions::intersects(*hull, *sphere));

  for (const Collider* other : std::initializer_list<const Collider*>{ box.get(), otherHull.get(), sphere.get() })
  {
    moveTo(*other, { 0, 1.9f, 0 });
  }

  EXPECT_TRUE(collisions::findContact(*hull, *box).has_value());
  EXPECT_TRUE(collisions::findContact(*hull, *otherHull).has_value());
  EXPECT_TRUE(collisions::findContact(*hull, *sphere).has_value());
  EXPECT_TRUE(collisions::intersects(*hull, *box));
  EXPECT_TRUE(collisions::intersects(*hull, *otherHull));
  EXPECT_TRUE(collisions::intersects(*hull, *sphere));
}

TEST(ConvexMeshNarrowPhase, AHullRestingFlatOnABoxGetsAFourPointManifoldInsideBothFootprints)
{
  const auto scene = fixtures::makeScene();
  const auto hull = hullAt(scene, { 0, 0, 0 });
  const auto box = boxAt(scene, { 0, 1.9f, 0 });

  const auto contact = collisions::findContact(*hull, *box);
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

  expectNear("point", contact->point, { 0, 1.0f, 0 }, tolerance);
}

TEST(ConvexMeshNarrowPhase, TheNormalPointsAwayFromTheOtherColliderWhicheverSideTheHullIsOn)
{
  const auto scene = fixtures::makeScene();
  const auto hull = hullAt(scene, { 0, 0, 0 });
  const auto box = boxAt(scene, { 0, 1.9f, 0 });

  const auto hullFirst = collisions::findContact(*hull, *box);
  const auto boxFirst = collisions::findContact(*box, *hull);

  ASSERT_TRUE(hullFirst.has_value());
  ASSERT_TRUE(boxFirst.has_value());

  // Each moves its own first collider clear: the hull down, the box up.
  expectNear("hull first", hullFirst->normal(), { 0, -1, 0 }, tolerance);
  expectNear("box first", boxFirst->normal(), { 0, 1, 0 }, tolerance);
  EXPECT_NEAR(boxFirst->depth(), 0.1f, tolerance);

  ASSERT_EQ(boxFirst->pointCount, 4);
  expectPointsInsideSquare(boxFirst->contactPoints(), -1.0f, 1.0f, -1.0f, 1.0f);
}

TEST(ConvexMeshNarrowPhase, AShiftedHullTouchesOnlyTheOverlappingPartOfTheFace)
{
  const auto scene = fixtures::makeScene();
  const auto hull = hullAt(scene, { 0, 0, 0 });
  const auto box = boxAt(scene, { 1.0f, 1.9f, 0 });

  const auto contact = collisions::findContact(*hull, *box);
  ASSERT_TRUE(contact.has_value());

  // The box's face spans x 0..2 and the hull's -1..1, so the clipped manifold spans x 0..1.
  ASSERT_EQ(contact->pointCount, 4);
  expectPointsInsideSquare(contact->contactPoints(), 0.0f, 1.0f, -1.0f, 1.0f);
  EXPECT_TRUE(hasPointNear(contact->contactPoints(), { 0.0f, 1.0f, -1.0f }));
  EXPECT_TRUE(hasPointNear(contact->contactPoints(), { 1.0f, 1.0f, 1.0f }));
}

TEST(ConvexMeshNarrowPhase, TwoHullsRestingFaceToFaceGetAFourPointManifold)
{
  const auto scene = fixtures::makeScene();
  const auto lower = hullAt(scene, { 0, 0, 0 });
  const auto upper = hullAt(scene, { 1.0f, 1.9f, 0 });

  const auto contact = collisions::findContact(*lower, *upper);
  ASSERT_TRUE(contact.has_value());

  EXPECT_NEAR(contact->depth(), 0.1f, tolerance);
  expectNear("normal", contact->normal(), { 0, -1, 0 }, tolerance);

  ASSERT_EQ(contact->pointCount, 4);
  expectPointsInsideSquare(contact->contactPoints(), 0.0f, 1.0f, -1.0f, 1.0f);

  const auto reversed = collisions::findContact(*upper, *lower);
  ASSERT_TRUE(reversed.has_value());
  expectNear("reversed normal", reversed->normal(), { 0, 1, 0 }, tolerance);
  ASSERT_EQ(reversed->pointCount, 4);
  expectPointsInsideSquare(reversed->contactPoints(), 0.0f, 1.0f, -1.0f, 1.0f);
}

TEST(ConvexMeshNarrowPhase, AHullOnItsEdgeOnABoxFaceGetsATwoPointManifold)
{
  // A box ground whose top is y = 1, and a hull cube turned 45 degrees about z so it stands on an edge
  // running along z, 0.5 long on each side of the middle. The lowest edge sits 0.05 below the ground top.
  const auto scene = fixtures::makeScene();
  const auto ground = boxAt(scene, { 0, 0, 0 });
  const auto hull = hullAt(scene, { 0, 1.0f + std::sqrt(2.0f) - 0.05f, 0 }, { 1, 1, 0.5f }, { 0, 0, 45 });

  const auto contact = collisions::findContact(*ground, *hull);
  ASSERT_TRUE(contact.has_value());

  EXPECT_NEAR(contact->depth(), 0.05f, tolerance);
  expectNear("normal", contact->normal(), { 0, -1, 0 }, tolerance);

  ASSERT_EQ(contact->pointCount, 2);
  EXPECT_TRUE(hasPointNear(contact->contactPoints(), { 0, 1.0f, -0.5f }));
  EXPECT_TRUE(hasPointNear(contact->contactPoints(), { 0, 1.0f, 0.5f }));
}

TEST(ConvexMeshNarrowPhase, ATetrahedronOnItsBaseTouchesTheBoxAcrossTheBase)
{
  const auto scene = fixtures::makeScene();
  const auto ground = boxAt(scene, { 0, 0, 0 });
  const auto tetrahedron = tetrahedronAt(scene, { 0, 2.0f - 0.05f, 0 });

  const auto contact = collisions::findContact(*ground, *tetrahedron);
  ASSERT_TRUE(contact.has_value());

  EXPECT_NEAR(contact->depth(), 0.05f, tolerance);
  expectNear("normal", contact->normal(), { 0, -1, 0 }, tolerance);

  // The base triangle has three corners, fewer than the box's four, so the box is the reference.
  ASSERT_EQ(contact->pointCount, 3);
  EXPECT_TRUE(hasPointNear(contact->contactPoints(), { -1, 1.0f, -1 }));
  EXPECT_TRUE(hasPointNear(contact->contactPoints(), { 1, 1.0f, -1 }));
  EXPECT_TRUE(hasPointNear(contact->contactPoints(), { 0, 1.0f, 1 }));
}

TEST(ConvexMeshNarrowPhase, AHullAndASphereMeetAtASinglePointOnTheSphereSideOfTheOverlap)
{
  const auto scene = fixtures::makeScene();
  const auto hull = hullAt(scene, { 0, 0, 0 });
  const auto sphere = fixtures::addSphereCollider(addObject(scene, "Sphere", { 0, 1.9f, 0 }), 1.0f);

  const auto hullFirst = collisions::findContact(*hull, *sphere);
  ASSERT_TRUE(hullFirst.has_value());

  EXPECT_NEAR(hullFirst->depth(), 0.1f, tolerance);
  expectNear("hull first normal", hullFirst->normal(), { 0, -1, 0 }, tolerance);
  EXPECT_EQ(hullFirst->pointCount, 0);
  expectNear("hull first point", hullFirst->point, { 0, 0.9f, 0 }, tolerance);

  const auto sphereFirst = collisions::findContact(*sphere, *hull);
  ASSERT_TRUE(sphereFirst.has_value());

  expectNear("sphere first normal", sphereFirst->normal(), { 0, 1, 0 }, tolerance);
  EXPECT_EQ(sphereFirst->pointCount, 0);
  expectNear("sphere first point", sphereFirst->point, { 0, 0.9f, 0 }, tolerance);
}

TEST(ConvexMeshNarrowPhase, ACapsuleLyingOnAHullFaceGetsATwoPointManifoldOnTheFace)
{
  // A capsule of radius 0.5 and height 2 lies along x, 0.05 into the cube's top face (y = 1). Its two cap
  // points sit 0.5 either side of the middle.
  const auto scene = fixtures::makeScene();
  const auto hull = hullAt(scene, { 0, 0, 0 });

  const auto capsuleObject = addObject(scene, "Capsule", { 0, 1.45f, 0 });
  transformOf(capsuleObject)->setRotation({ 0, 0, 90 });
  const auto capsule = fixtures::addCapsuleCollider(capsuleObject, 0.5f, 2.0f);

  // GJK and EPA converge on this pair rather than solving it, like the capsule suite's looser bound.
  constexpr float converged = 2e-2f;

  for (const bool hullFirst : { true, false })
  {
    SCOPED_TRACE(::testing::Message() << "hull first " << hullFirst);

    const auto contact = hullFirst ? collisions::findContact(*hull, *capsule)
                                   : collisions::findContact(*capsule, *hull);
    ASSERT_TRUE(contact.has_value());

    EXPECT_NEAR(contact->depth(), 0.05f, converged);
    ASSERT_EQ(contact->pointCount, 2);
    EXPECT_TRUE(hasPointNear(contact->contactPoints(), { -0.5f, 1.0f, 0.0f }, converged));
    EXPECT_TRUE(hasPointNear(contact->contactPoints(), { 0.5f, 1.0f, 0.0f }, converged));
  }

  moveTo(*capsule, { 0, 4.0f, 0 });
  EXPECT_FALSE(collisions::findContact(*hull, *capsule).has_value());
}

TEST(ConvexMeshNarrowPhase, AScaledHullTakesItsSizeFromTheTransformLikeABox)
{
  // Spans x -3..3 once scaled, so a unit box centered at 3.5 overlaps it by 0.5 - only true if the scale
  // reached the hull's world vertices.
  const auto scene = fixtures::makeScene();
  const auto hull = hullAt(scene, { 0, 0, 0 }, { 3, 1, 1 });
  const auto box = boxAt(scene, { 3.5f, 0, 0 });

  const auto contact = collisions::findContact(*hull, *box);
  ASSERT_TRUE(contact.has_value());

  EXPECT_NEAR(contact->depth(), 0.5f, tolerance);
  expectNear("normal", contact->normal(), { -1, 0, 0 }, tolerance);
}
