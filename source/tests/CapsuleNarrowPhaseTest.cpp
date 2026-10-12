#include <gtest/gtest.h>

#include "TestPrinters.h"
#include "TestScene.h"
#include "collisions/NarrowPhase.h"
#include "objects/Object.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/CapsuleCollider.h"
#include "objects/components/collisions/SphereCollider.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <memory>
#include <vector>

namespace {
  using fixtures::addObject;
  using fixtures::expectNear;

  // Analytic pairs are exact up to float noise; GJK/EPA pairs converge, so they get the looser one.
  constexpr float exact = 1e-4f;
  constexpr float converged = 2e-2f;

  // A capsule of radius 0.5 and total height 2 (core segment half-length 0.5), upright unless turned.
  std::shared_ptr<CapsuleCollider> capsuleAt(const fixtures::Scene& scene, const glm::vec3& position,
                                             const glm::vec3& rotation = glm::vec3(0))
  {
    const auto object = addObject(scene, "Capsule", position);
    fixtures::transformOf(object)->setRotation(rotation);

    return fixtures::addCapsuleCollider(object, 0.5f, 2.0f);
  }

  std::shared_ptr<SphereCollider> sphereAt(const fixtures::Scene& scene, const glm::vec3& position,
                                           const float radius = 0.5f)
  {
    const auto object = addObject(scene, "Sphere", position);

    return fixtures::addSphereCollider(object, radius);
  }

  // The unit box [-1,1]^3 at the origin: its top face is y = 1.
  std::shared_ptr<BoxCollider> groundBox(const fixtures::Scene& scene)
  {
    return fixtures::addBoxCollider(addObject(scene, "Ground", { 0, 0, 0 }));
  }

  std::vector<glm::vec3> sortedByX(const collisions::Contact& contact)
  {
    const auto points = contact.contactPoints();
    std::vector<glm::vec3> sorted(points.begin(), points.end());
    std::ranges::sort(sorted, [](const glm::vec3& a, const glm::vec3& b) { return a.x < b.x; });

    return sorted;
  }

  void moveTo(const Collider& collider, const glm::vec3& position)
  {
    collider.getOwner()->getComponent<Transform>(ComponentType::transform)->setPosition(position);
  }

  void expectAgree(Collider& first, Collider& second)
  {
    EXPECT_EQ(collisions::findContact(first, second).has_value(), collisions::intersects(first, second));
    EXPECT_EQ(collisions::findContact(second, first).has_value(), collisions::intersects(second, first));
  }
}

TEST(CapsuleNarrowPhase, ASphereBesideTheShaftIsPushedOutAlongTheShaftNormalWhicheverOrder)
{
  const auto scene = fixtures::makeScene();
  const auto capsule = capsuleAt(scene, { 0, 0, 0 });
  const auto sphere = sphereAt(scene, { 0.8f, 0, 0 });

  const auto sphereFirst = collisions::findContact(*sphere, *capsule);
  ASSERT_TRUE(sphereFirst.has_value());
  expectNear("sphere first mtv", sphereFirst->minimumTranslationVector, { 0.2f, 0, 0 }, exact);
  expectNear("sphere first point", sphereFirst->point, { 0.4f, 0, 0 }, exact);
  EXPECT_EQ(sphereFirst->pointCount, 0);

  const auto capsuleFirst = collisions::findContact(*capsule, *sphere);
  ASSERT_TRUE(capsuleFirst.has_value());
  expectNear("capsule first mtv", capsuleFirst->minimumTranslationVector, { -0.2f, 0, 0 }, exact);
  expectNear("capsule first point", capsuleFirst->point, { 0.4f, 0, 0 }, exact);

  EXPECT_TRUE(collisions::intersects(*sphere, *capsule));
  EXPECT_TRUE(collisions::intersects(*capsule, *sphere));
}

TEST(CapsuleNarrowPhase, ASphereAboveTheCapRestsOnTheEndNotTheShaft)
{
  const auto scene = fixtures::makeScene();
  const auto capsule = capsuleAt(scene, { 0, 0, 0 });
  const auto sphere = sphereAt(scene, { 0, 1.3f, 0 });

  const auto contact = collisions::findContact(*capsule, *sphere);
  ASSERT_TRUE(contact.has_value());

  expectNear("mtv", contact->minimumTranslationVector, { 0, -0.2f, 0 }, exact);
  expectNear("point", contact->point, { 0, 0.9f, 0 }, exact);
}

TEST(CapsuleNarrowPhase, TheShaftEndsWhereTheSegmentDoes)
{
  const auto scene = fixtures::makeScene();
  const auto capsule = capsuleAt(scene, { 0, 0, 0 });

  // 0.9 from the axis but 0.7 above the segment's end: farther than the two radii from the cap's center, though
  // an infinite cylinder of the same radius would have been touched.
  const auto apart = sphereAt(scene, { 0.9f, 1.2f, 0 });
  EXPECT_FALSE(collisions::findContact(*capsule, *apart).has_value());
  EXPECT_FALSE(collisions::intersects(*capsule, *apart));
  EXPECT_FALSE(collisions::intersects(*apart, *capsule));

  // Positive control: lowered into reach of the cap's center.
  const auto touching = sphereAt(scene, { 0.9f, 0.9f, 0 });
  EXPECT_TRUE(collisions::findContact(*capsule, *touching).has_value());
  EXPECT_TRUE(collisions::intersects(*capsule, *touching));
}

TEST(CapsuleNarrowPhase, AZeroLengthCapsuleTouchesLikeASphereOfTheSameRadius)
{
  const auto scene = fixtures::makeScene();
  const auto capsule = capsuleAt(scene, { 0, 0, 0 });
  capsule->setHeight(1.0f);
  const auto other = sphereAt(scene, { 0.8f, 0.1f, 0 });
  const auto reference = sphereAt(scene, { 0, 0, 0 }, 0.5f);

  const auto fromCapsule = collisions::findContact(*capsule, *other);
  const auto fromSphere = collisions::findContact(*reference, *other);
  ASSERT_TRUE(fromCapsule.has_value());
  ASSERT_TRUE(fromSphere.has_value());

  expectNear("mtv", fromCapsule->minimumTranslationVector, fromSphere->minimumTranslationVector, exact);
  expectNear("point", fromCapsule->point, fromSphere->point, exact);
}

TEST(CapsuleNarrowPhase, CrossedCapsulesReportOnePointAndPushTheFirstOneAway)
{
  const auto scene = fixtures::makeScene();
  const auto upright = capsuleAt(scene, { 0, 0, 0 });
  const auto lying = capsuleAt(scene, { 0, 0.8f, 0 }, { 0, 0, 90 });

  const auto first = collisions::findContact(*upright, *lying);
  ASSERT_TRUE(first.has_value());
  expectNear("upright mtv", first->minimumTranslationVector, { 0, -0.7f, 0 }, exact);
  expectNear("point", first->point, { 0, 0.65f, 0 }, exact);
  EXPECT_EQ(first->pointCount, 0);

  const auto second = collisions::findContact(*lying, *upright);
  ASSERT_TRUE(second.has_value());
  expectNear("lying mtv", second->minimumTranslationVector, { 0, 0.7f, 0 }, exact);
  expectNear("point", second->point, { 0, 0.65f, 0 }, exact);
}

TEST(CapsuleNarrowPhase, CapsulesCrossingAtTheirAxesStillGetANonZeroPush)
{
  const auto scene = fixtures::makeScene();
  const auto upright = capsuleAt(scene, { 0, 0, 0 });
  const auto lying = capsuleAt(scene, { 0, 0, 0 }, { 0, 0, 90 });

  const auto contact = collisions::findContact(*upright, *lying);
  ASSERT_TRUE(contact.has_value());

  // Exactly coincident axes fall back to a half-radius-sum push; float noise in the turn gives the full depth.
  EXPECT_GT(contact->depth(), 0.4f);
  EXPECT_TRUE(collisions::intersects(*upright, *lying));
}

TEST(CapsuleNarrowPhase, ParallelCapsulesSideBySideReportBothEndsOfTheirOverlap)
{
  const auto scene = fixtures::makeScene();
  const auto first = capsuleAt(scene, { 0, 0, 0 });
  const auto second = capsuleAt(scene, { 0.8f, 0.3f, 0 });

  // The segments overlap from y = -0.2 (the second's low end) to 0.5 (the first's high end).
  const auto contact = collisions::findContact(*first, *second);
  ASSERT_TRUE(contact.has_value());

  expectNear("mtv", contact->minimumTranslationVector, { -0.2f, 0, 0 }, exact);
  ASSERT_EQ(contact->pointCount, 2);

  auto points = std::vector<glm::vec3>(contact->points.begin(), contact->points.begin() + 2);
  std::ranges::sort(points, [](const glm::vec3& a, const glm::vec3& b) { return a.y < b.y; });
  expectNear("low end", points[0], { 0.4f, -0.2f, 0 }, exact);
  expectNear("high end", points[1], { 0.4f, 0.5f, 0 }, exact);

  const auto swapped = collisions::findContact(*second, *first);
  ASSERT_TRUE(swapped.has_value());
  expectNear("swapped mtv", swapped->minimumTranslationVector, { 0.2f, 0, 0 }, exact);
  EXPECT_EQ(swapped->pointCount, 2);
}

TEST(CapsuleNarrowPhase, CapsulesEndToEndTouchAtASinglePoint)
{
  const auto scene = fixtures::makeScene();
  const auto lower = capsuleAt(scene, { 0, 0, 0 });
  const auto upper = capsuleAt(scene, { 0, 1.2f, 0 });

  const auto contact = collisions::findContact(*lower, *upper);
  ASSERT_TRUE(contact.has_value());

  expectNear("mtv", contact->minimumTranslationVector, { 0, -0.8f, 0 }, exact);
  EXPECT_EQ(contact->pointCount, 0);
}

TEST(CapsuleNarrowPhase, SeparatedCapsulesReportNothingAndANearerPairDoes)
{
  const auto scene = fixtures::makeScene();
  const auto first = capsuleAt(scene, { 0, 0, 0 });
  const auto apart = capsuleAt(scene, { 1.2f, 0.3f, 0 });
  const auto crossedApart = capsuleAt(scene, { 0, 1.7f, 0 }, { 0, 0, 90 });

  EXPECT_FALSE(collisions::findContact(*first, *apart).has_value());
  EXPECT_FALSE(collisions::intersects(*first, *apart));
  EXPECT_FALSE(collisions::findContact(*first, *crossedApart).has_value());
  EXPECT_FALSE(collisions::intersects(*crossedApart, *first));

  // Positive control: the same capsules brought into reach.
  moveTo(*apart, { 0.8f, 0.3f, 0 });
  EXPECT_TRUE(collisions::findContact(*first, *apart).has_value());
  EXPECT_TRUE(collisions::intersects(*first, *apart));
}

TEST(CapsuleNarrowPhase, FindContactAndIntersectsAgreeAcrossASweepOfOffsets)
{
  const auto scene = fixtures::makeScene();
  const auto capsule = capsuleAt(scene, { 0, 0, 0 });
  const auto sphere = sphereAt(scene, { 0, 0, 0 });
  const auto other = capsuleAt(scene, { 0, 0, 0 }, { 0, 0, 90 });

  bool sawContact = false;
  bool sawGap = false;

  for (int step = 0; step <= 20; ++step)
  {
    const float offset = 0.13f * static_cast<float>(step);
    moveTo(*sphere, { offset, 0.4f, 0.1f });
    moveTo(*other, { 0.1f, offset, 0.3f });

    expectAgree(*capsule, *sphere);
    expectAgree(*capsule, *other);

    sawContact |= collisions::intersects(*capsule, *sphere);
    sawGap |= !collisions::intersects(*capsule, *sphere);
  }

  EXPECT_TRUE(sawContact);
  EXPECT_TRUE(sawGap);
}

TEST(CapsuleNarrowPhase, AStandingCapsuleOnABoxTouchesAtOnePointWhicheverOrder)
{
  const auto scene = fixtures::makeScene();
  const auto box = groundBox(scene);
  const auto capsule = capsuleAt(scene, { 0, 1.9f, 0 });

  const auto capsuleFirst = collisions::findContact(*capsule, *box);
  ASSERT_TRUE(capsuleFirst.has_value());
  EXPECT_NEAR(capsuleFirst->depth(), 0.1f, converged);
  EXPECT_GT(capsuleFirst->normal().y, 0.99f);
  expectNear("point", capsuleFirst->point, { 0, 0.9f, 0 }, converged);
  EXPECT_EQ(capsuleFirst->pointCount, 0);

  const auto boxFirst = collisions::findContact(*box, *capsule);
  ASSERT_TRUE(boxFirst.has_value());
  EXPECT_NEAR(boxFirst->depth(), 0.1f, converged);
  EXPECT_LT(boxFirst->normal().y, -0.99f);
  expectNear("point", boxFirst->point, { 0, 0.9f, 0 }, converged);
  EXPECT_EQ(boxFirst->pointCount, 0);
}

TEST(CapsuleNarrowPhase, ACapsuleLyingOnABoxTouchesAtBothEndsWhicheverOrder)
{
  const auto scene = fixtures::makeScene();
  const auto box = groundBox(scene);
  const auto capsule = capsuleAt(scene, { 0, 1.4f, 0 }, { 0, 0, 90 });

  const auto capsuleFirst = collisions::findContact(*capsule, *box);
  ASSERT_TRUE(capsuleFirst.has_value());
  EXPECT_NEAR(capsuleFirst->depth(), 0.1f, converged);
  EXPECT_GT(capsuleFirst->normal().y, 0.99f);
  ASSERT_EQ(capsuleFirst->pointCount, 2);

  const auto points = sortedByX(*capsuleFirst);
  expectNear("left end", points[0], { -0.5f, 0.9f, 0 }, converged);
  expectNear("right end", points[1], { 0.5f, 0.9f, 0 }, converged);

  const auto boxFirst = collisions::findContact(*box, *capsule);
  ASSERT_TRUE(boxFirst.has_value());
  EXPECT_LT(boxFirst->normal().y, -0.99f);
  ASSERT_EQ(boxFirst->pointCount, 2);
  EXPECT_NEAR(sortedByX(*boxFirst)[0].x, -0.5f, converged);
  EXPECT_NEAR(sortedByX(*boxFirst)[1].x, 0.5f, converged);
}

TEST(CapsuleNarrowPhase, ACapsuleOverhangingABoxHasEveryContactPointOnTheBoxFootprint)
{
  const auto scene = fixtures::makeScene();
  const auto box = groundBox(scene);
  const auto capsule = capsuleAt(scene, { 1.2f, 1.4f, 0 }, { 0, 0, 90 });

  const auto contact = collisions::findContact(*capsule, *box);
  ASSERT_TRUE(contact.has_value());
  ASSERT_EQ(contact->pointCount, 2);

  // The capsule's ends are at x = 0.7 and 1.7; the far one is pulled back to the box's edge at 1.
  const auto points = sortedByX(*contact);
  EXPECT_NEAR(points[0].x, 0.7f, converged);
  EXPECT_NEAR(points[1].x, 1.0f, converged);

  for (const auto& point : points)
  {
    EXPECT_LE(point.x, 1.0f + converged);
    EXPECT_GE(point.x, -1.0f - converged);
  }
}

TEST(CapsuleNarrowPhase, ACapsuleAboveABoxDoesNotTouchUntilItIsLowered)
{
  const auto scene = fixtures::makeScene();
  const auto box = groundBox(scene);
  const auto capsule = capsuleAt(scene, { 0, 2.6f, 0 });

  EXPECT_FALSE(collisions::findContact(*capsule, *box).has_value());
  EXPECT_FALSE(collisions::intersects(*capsule, *box));
  EXPECT_FALSE(collisions::intersects(*box, *capsule));

  moveTo(*capsule, { 0, 1.9f, 0 });
  EXPECT_TRUE(collisions::findContact(*capsule, *box).has_value());
  EXPECT_TRUE(collisions::intersects(*capsule, *box));
  EXPECT_TRUE(collisions::intersects(*box, *capsule));
}
