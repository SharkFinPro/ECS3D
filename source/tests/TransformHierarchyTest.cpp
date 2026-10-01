#include <gtest/gtest.h>

#include "TestScene.h"
#include "objects/Object.h"
#include "objects/WorldPlacement.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <memory>

namespace {
  using fixtures::addBoxCollider;
  using fixtures::expectNear;
  using fixtures::transformOf;

  glm::quat orientationFromEuler(const glm::vec3& degrees)
  {
    return glm::quat(glm::radians(degrees));
  }

  // q and -q are one rotation, and an Euler triple has several spellings, so orientations are compared by
  // where they send the basis vectors.
  void expectSameOrientation(const glm::quat& actual, const glm::quat& expected, const float tolerance = 1e-4f)
  {
    expectNear("x axis", actual * glm::vec3(1, 0, 0), expected * glm::vec3(1, 0, 0), tolerance);
    expectNear("y axis", actual * glm::vec3(0, 1, 0), expected * glm::vec3(0, 1, 0), tolerance);
    expectNear("z axis", actual * glm::vec3(0, 0, 1), expected * glm::vec3(0, 0, 1), tolerance);
  }

  void expectWorldRotation(const std::shared_ptr<Object>& object, const glm::quat& expected)
  {
    expectSameOrientation(orientationFromEuler(transformOf(object)->getRotation()), expected);
    expectSameOrientation(transformOf(object)->getOrientation(), expected);
  }
}

TEST(TransformHierarchy, AChildOfATurnedParentIsPlacedInTheParentsTurnedFrame)
{
  const auto scene = fixtures::makeScene();
  const auto parent = addObject(scene, "Parent", glm::vec3(0));
  transformOf(parent)->setRotation(glm::vec3(0, 90, 0));
  const auto child = addChildObject(scene, "Child", parent);
  transformOf(child)->setPosition(glm::vec3(1, 0, 0));

  expectNear(transformOf(child)->getPosition(), glm::vec3(0, 0, -1));
}

TEST(TransformHierarchy, AChildOfAScaledParentIsPlacedAtTheScaledOffset)
{
  const auto scene = fixtures::makeScene();
  const auto parent = addObject(scene, "Parent", glm::vec3(10, 0, 0), glm::vec3(2));
  const auto child = addChildObject(scene, "Child", parent);
  transformOf(child)->setPosition(glm::vec3(1, 0, 0));

  expectNear(transformOf(child)->getPosition(), glm::vec3(12, 0, 0));
}

TEST(TransformHierarchy, ScaleTurnAndOffsetCompoundThroughAGrandparent)
{
  const auto scene = fixtures::makeScene();
  const auto grandparent = addObject(scene, "Grandparent", glm::vec3(0, 5, 0), glm::vec3(2));
  transformOf(grandparent)->setRotation(glm::vec3(0, 90, 0));
  const auto parent = addChildObject(scene, "Parent", grandparent);
  transformOf(parent)->setPosition(glm::vec3(1, 0, 0));
  const auto child = addChildObject(scene, "Child", parent);
  transformOf(child)->setPosition(glm::vec3(1, 0, 0));

  // The parent sits at (0, 5, -2); the child is one more unit along the turned x, scaled by 2.
  expectNear(transformOf(child)->getPosition(), glm::vec3(0, 5, -4));
}

TEST(TransformHierarchy, ChildOrientationIsTheQuaternionCompositionNotTheEulerSum)
{
  const auto scene = fixtures::makeScene();
  const auto parent = addObject(scene, "Parent", glm::vec3(0));
  transformOf(parent)->setRotation(glm::vec3(90, 0, 0));
  const auto child = addChildObject(scene, "Child", parent);
  transformOf(child)->setRotation(glm::vec3(0, 90, 0));

  const auto composed = orientationFromEuler(glm::vec3(90, 0, 0)) * orientationFromEuler(glm::vec3(0, 90, 0));
  expectWorldRotation(child, composed);

  // The Euler sum (90, 90, 0) is a different orientation, so a getter that added angles fails above.
  const auto summed = orientationFromEuler(glm::vec3(90, 90, 0));
  EXPECT_GT(glm::length(composed * glm::vec3(1, 0, 0) - summed * glm::vec3(1, 0, 0)), 0.5f);
}

TEST(TransformHierarchy, ARootObjectReturnsItsLocalValuesExactly)
{
  const auto scene = fixtures::makeScene();
  const auto root = addObject(scene, "Root", glm::vec3(1, 2, 3), glm::vec3(4, 5, 6));
  transformOf(root)->setRotation(glm::vec3(200, 120, -250));

  EXPECT_EQ(transformOf(root)->getPosition(), glm::vec3(1, 2, 3));
  EXPECT_EQ(transformOf(root)->getScale(), glm::vec3(4, 5, 6));
  EXPECT_EQ(transformOf(root)->getRotation(), glm::vec3(200, 120, -250));
}

TEST(TransformHierarchy, SettingAWorldRotationUnderATurnedParentRoundTrips)
{
  const auto scene = fixtures::makeScene();
  const auto parent = addObject(scene, "Parent", glm::vec3(0));
  transformOf(parent)->setRotation(glm::vec3(30, 60, 10));
  const auto child = addChildObject(scene, "Child", parent);

  const auto wantedEuler = glm::vec3(20, -40, 70);
  transformOf(child)->setWorldRotation(wantedEuler);

  expectWorldRotation(child, orientationFromEuler(wantedEuler));

  // The local rotation is not the world one, so a setter that stored it unchanged would not get here.
  EXPECT_GT(glm::length(transformOf(child)->getLocalRotation() - wantedEuler), 1.0f);
}

TEST(TransformHierarchy, ReparentingBetweenDifferentlyTurnedAndScaledParentsKeepsWorldPlacement)
{
  const auto scene = fixtures::makeScene();
  const auto parentA = addObject(scene, "A", glm::vec3(3, 1, -2), glm::vec3(2, 2, 2));
  transformOf(parentA)->setRotation(glm::vec3(0, 40, 0));
  const auto parentB = addObject(scene, "B", glm::vec3(-6, 4, 8), glm::vec3(0.5f, 0.5f, 0.5f));
  transformOf(parentB)->setRotation(glm::vec3(25, 0, 70));

  const auto child = addChildObject(scene, "Child", parentA);
  transformOf(child)->setPosition(glm::vec3(1, 2, 3));
  transformOf(child)->setRotation(glm::vec3(10, 20, 30));
  transformOf(child)->setScale(glm::vec3(1.5f));

  const auto before = captureWorldPlacement(child);
  ASSERT_TRUE(before.has_value());
  const auto orientationBefore = transformOf(child)->getOrientation();

  parentA->removeChild(child);
  child->setParent(parentB);
  parentB->addChild(child);
  restoreWorldPlacement(child, parentB, *before);

  expectNear("position", transformOf(child)->getPosition(), before->position, 1e-4f);
  expectNear("scale", transformOf(child)->getScale(), before->scale, 1e-4f);
  expectSameOrientation(transformOf(child)->getOrientation(), orientationBefore);

  // Positive control: the local position had to change for the world one to hold.
  EXPECT_GT(glm::length(transformOf(child)->getLocalPosition() - glm::vec3(1, 2, 3)), 1.0f);
}

TEST(TransformHierarchy, MovingByAWorldDisplacementUnderATurnedScaledParentTravelsThatDistance)
{
  const auto scene = fixtures::makeScene();
  const auto parent = addObject(scene, "Parent", glm::vec3(5, 0, 0), glm::vec3(2));
  transformOf(parent)->setRotation(glm::vec3(0, 90, 0));
  const auto child = addChildObject(scene, "Child", parent);

  transformOf(child)->moveWorld(glm::vec3(0, 0, -4));

  expectNear(transformOf(child)->getPosition(), glm::vec3(5, 0, -4));
}

TEST(TransformHierarchy, ABoxColliderOnAChildFollowsATurnedParent)
{
  const auto scene = fixtures::makeScene();
  const auto parent = addObject(scene, "Parent", glm::vec3(0));
  transformOf(parent)->setRotation(glm::vec3(0, 90, 0));
  const auto child = addChildObject(scene, "Child", parent);
  transformOf(child)->setPosition(glm::vec3(2, 0, 0));
  const auto box = addBoxCollider(child);

  const auto& bounds = box->getBoundingBox();

  EXPECT_NEAR(bounds.minX, -1.0f, 1e-4f);
  EXPECT_NEAR(bounds.maxX, 1.0f, 1e-4f);
  EXPECT_NEAR(bounds.minY, -1.0f, 1e-4f);
  EXPECT_NEAR(bounds.maxY, 1.0f, 1e-4f);
  EXPECT_NEAR(bounds.minZ, -3.0f, 1e-4f);
  EXPECT_NEAR(bounds.maxZ, -1.0f, 1e-4f);

  // Tilted off the axes so the answer is one corner rather than a tie between four.
  expectNear(box->findFurthestPoint(glm::vec3(0.1f, 0.1f, 1.0f)), glm::vec3(1, 1, -1), 1e-4f);
}

TEST(TransformHierarchy, ABoxColliderOrientationComposesWithItsParentsTurn)
{
  const auto scene = fixtures::makeScene();
  const auto parent = addObject(scene, "Parent", glm::vec3(0));
  transformOf(parent)->setRotation(glm::vec3(90, 0, 0));
  const auto child = addChildObject(scene, "Child", parent);
  transformOf(child)->setRotation(glm::vec3(0, 90, 0));
  const auto box = addBoxCollider(child);

  const auto composed = orientationFromEuler(glm::vec3(90, 0, 0)) * orientationFromEuler(glm::vec3(0, 90, 0));
  expectSameOrientation(orientationFromEuler(box->getRotation()), composed);
}
