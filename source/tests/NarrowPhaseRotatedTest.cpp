#include <gtest/gtest.h>

#include "TestPrinters.h"
#include "collisions/NarrowPhase.h"
#include "objects/Object.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat3x3.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace {
  constexpr float tolerance = 1e-3f;

  // A box is the unit box [-1,1]^3 scaled, rotated (degrees, applied X then Y then Z) and moved, so its
  // half extents are its scale. The returned Object anchors the Transform the collider points at.
  struct Box
  {
    std::shared_ptr<Object> object;
    std::shared_ptr<BoxCollider> collider;
    glm::vec3 position;
    glm::vec3 scale;
    glm::vec3 rotation;
  };

  Box makeBox(const glm::vec3& position, const glm::vec3& rotation = glm::vec3(0),
              const glm::vec3& scale = glm::vec3(1))
  {
    auto object = std::make_shared<Object>("Collider");
    const auto transform = object->getComponent<Transform>(ComponentType::transform);
    transform->setPosition(position);
    transform->setRotation(rotation);
    transform->setScale(scale);

    auto collider = std::make_shared<BoxCollider>();
    object->addComponent(collider);

    return { object, collider, position, scale, rotation };
  }

  glm::mat3 orientationOf(const glm::vec3& rotationDegrees)
  {
    const auto radians = glm::radians(rotationDegrees);

    return glm::mat3(glm::rotate(glm::mat4(1.0f), radians.z, { 0, 0, 1 }) *
                     glm::rotate(glm::mat4(1.0f), radians.y, { 0, 1, 0 }) *
                     glm::rotate(glm::mat4(1.0f), radians.x, { 1, 0, 0 }));
  }

  bool insideBox(const Box& box, const glm::vec3& point, const float slack = tolerance)
  {
    const auto orientation = orientationOf(box.rotation);
    const auto relative = point - box.position;

    for (int i = 0; i < 3; ++i)
    {
      if (std::fabs(glm::dot(relative, orientation[i])) > box.scale[i] + slack)
      {
        return false;
      }
    }

    return true;
  }

  bool isFinite(const glm::vec3& value)
  {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
  }

  void expectNear(const char* what, const glm::vec3& actual, const glm::vec3& expected, const float slack = tolerance)
  {
    SCOPED_TRACE(::testing::Message()
                 << what
                 << ": expected (" << expected.x << ", " << expected.y << ", " << expected.z << ")"
                 << ", got (" << actual.x << ", " << actual.y << ", " << actual.z << ")");

    EXPECT_NEAR(actual.x, expected.x, slack);
    EXPECT_NEAR(actual.y, expected.y, slack);
    EXPECT_NEAR(actual.z, expected.z, slack);
  }

  // The cheap predicate and the full query have to agree on every pair the sweep could hand them.
  void expectAgreement(Box& first, Box& second, const bool touching)
  {
    EXPECT_EQ(collisions::intersects(*first.collider, *second.collider), touching);
    EXPECT_EQ(collisions::findContact(*first.collider, *second.collider).has_value(), touching);
  }

  bool hasPointNear(const collisions::Contact& contact, const glm::vec3& expected)
  {
    for (const auto& point : contact.contactPoints())
    {
      if (glm::distance(point, expected) <= tolerance)
      {
        return true;
      }
    }

    return false;
  }

  void expectEveryPointInsideBoth(const collisions::Contact& contact, const Box& first, const Box& second)
  {
    for (const auto& point : contact.contactPoints())
    {
      SCOPED_TRACE(::testing::Message() << "point (" << point.x << ", " << point.y << ", " << point.z << ")");

      EXPECT_TRUE(insideBox(first, point));
      EXPECT_TRUE(insideBox(second, point));
    }
  }

  // The rotation 30 degrees about X then 45 about Y, as unit vectors: where the local X, Y and Z axes land.
  constexpr float invSqrt2 = 0.70710678f;
  const std::array<glm::vec3, 3> tiltedAxes = {
    glm::vec3(invSqrt2, 0.0f, -invSqrt2),
    glm::vec3(0.5f * invSqrt2, 0.8660254f, 0.5f * invSqrt2),
    glm::vec3(0.8660254f * invSqrt2, -0.5f, 0.8660254f * invSqrt2)
  };
}

TEST(NarrowPhaseRotated, ReportsABoxTurnedAboutYRestingOnAFlatBoxAlongY)
{
  auto flat = makeBox({ 0, 0, 0 });

  // The turned box's bottom face is still horizontal, so it sits at y = 0.8 against the flat box's top
  // at y = 1: 0.2 deep. Every other axis is more than two deep (the diamond footprint reaches
  // 1 + sqrt(2) past the flat box's center on x and z).
  auto turned = makeBox({ 0, 1.8f, 0 }, { 0, 45, 0 });

  const auto contact = collisions::findContact(*flat.collider, *turned.collider);
  ASSERT_TRUE(contact.has_value());

  EXPECT_NEAR(contact->depth(), 0.2f, tolerance);
  expectNear("normal", contact->normal(), { 0, -1, 0 });

  expectAgreement(flat, turned, true);

  // Seen from the turned box the normal flips, and the depth does not.
  const auto reversed = collisions::findContact(*turned.collider, *flat.collider);
  ASSERT_TRUE(reversed.has_value());

  EXPECT_NEAR(reversed->depth(), 0.2f, tolerance);
  expectNear("reversed normal", reversed->normal(), { 0, 1, 0 });

  // The control: lifted clear, the same pair no longer touches.
  turned.object->getComponent<Transform>(ComponentType::transform)->setPosition({ 0, 2.2f, 0 });
  turned.position = { 0, 2.2f, 0 };

  expectAgreement(flat, turned, false);
}

TEST(NarrowPhaseRotated, MeasuresASidewaysPushOfATurnedBoxFromItsCorner)
{
  auto flat = makeBox({ 0, 0, 0 });

  // Turned 45 degrees about Y, the box's nearest corner points back along -x, sqrt(2) from its center.
  // Centered at x = 2 that corner sits at x = 2 - sqrt(2) = 0.586, which is 1 - 0.586 = 0.414 inside the
  // flat box's +x face. The turned box's own face normals overlap by exactly 1, and y and z by more.
  auto turned = makeBox({ 2, 0, 0 }, { 0, 45, 0 });

  const auto contact = collisions::findContact(*flat.collider, *turned.collider);
  ASSERT_TRUE(contact.has_value());

  EXPECT_NEAR(contact->depth(), std::sqrt(2.0f) - 1.0f, tolerance);
  expectNear("normal", contact->normal(), { -1, 0, 0 });

  expectAgreement(flat, turned, true);

  // The mirrored push, so the sign is pinned on both sides.
  turned.object->getComponent<Transform>(ComponentType::transform)->setPosition({ -2, 0, 0 });
  turned.position = { -2, 0, 0 };

  const auto mirrored = collisions::findContact(*flat.collider, *turned.collider);
  ASSERT_TRUE(mirrored.has_value());

  EXPECT_NEAR(mirrored->depth(), std::sqrt(2.0f) - 1.0f, tolerance);
  expectNear("mirrored normal", mirrored->normal(), { 1, 0, 0 });
}

TEST(NarrowPhaseRotated, FollowsTheSharedFaceNormalOfTwoBoxesTurnedAboutTwoAxes)
{
  const glm::vec3 rotation{ 30, 45, 0 };

  for (int axis = 0; axis < 3; ++axis)
  {
    SCOPED_TRACE(::testing::Message() << "offset along local axis " << axis);

    // Identical orientations, so the local axis is a face normal of both. Offset along it by 1.5 of the
    // 2 the pair spans, and the overlap there is 0.5; every other face axis overlaps the full 2 and the
    // edge pairs reduce to those same axes.
    const auto& direction = tiltedAxes[axis];

    auto first = makeBox({ 0, 0, 0 }, rotation);
    auto second = makeBox(1.5f * direction, rotation);

    const auto contact = collisions::findContact(*first.collider, *second.collider);
    ASSERT_TRUE(contact.has_value());

    EXPECT_NEAR(contact->depth(), 0.5f, tolerance);
    expectNear("normal", contact->normal(), -direction);

    expectAgreement(first, second, true);
  }
}

TEST(NarrowPhaseRotated, ReportsFourManifoldPointsForFaceOnFace)
{
  auto flat = makeBox({ 0, 0, 0 });

  // The upper box's bottom face (y = 0.8) overlaps the flat box's top over x in [-0.5, 1] and z in
  // [-0.5, 1]; its four corners are clamped onto that footprint.
  auto upper = makeBox({ 0.5f, 1.8f, 0.5f });

  const auto contact = collisions::findContact(*flat.collider, *upper.collider);
  ASSERT_TRUE(contact.has_value());

  EXPECT_NEAR(contact->depth(), 0.2f, tolerance);
  expectNear("normal", contact->normal(), { 0, -1, 0 });

  EXPECT_EQ(contact->pointCount, 4);
  EXPECT_EQ(contact->contactPoints().size(), 4u);

  for (const float x : { -0.5f, 1.0f })
  {
    for (const float z : { -0.5f, 1.0f })
    {
      EXPECT_TRUE(hasPointNear(*contact, { x, 0.8f, z })) << "missing (" << x << ", 0.8, " << z << ")";
    }
  }

  expectEveryPointInsideBoth(*contact, flat, upper);
}

TEST(NarrowPhaseRotated, ReportsTwoManifoldPointsForEdgeOnFace)
{
  auto flat = makeBox({ 0, 0, 0 });

  // Turned 45 degrees about X, the box balances on an edge that runs along x at y = center - sqrt(2).
  // Centered at 1 + sqrt(2) - 0.1 the edge is 0.1 below the flat box's top.
  const float centerY = 1.0f + std::sqrt(2.0f) - 0.1f;
  auto tilted = makeBox({ 0, centerY, 0 }, { 45, 0, 0 });

  const auto contact = collisions::findContact(*flat.collider, *tilted.collider);
  ASSERT_TRUE(contact.has_value());

  EXPECT_NEAR(contact->depth(), 0.1f, tolerance);
  expectNear("normal", contact->normal(), { 0, -1, 0 });

  EXPECT_EQ(contact->pointCount, 2);
  EXPECT_EQ(contact->contactPoints().size(), 2u);
  EXPECT_TRUE(hasPointNear(*contact, { -1, 0.9f, 0 }));
  EXPECT_TRUE(hasPointNear(*contact, { 1, 0.9f, 0 }));

  expectEveryPointInsideBoth(*contact, flat, tilted);

  expectAgreement(flat, tilted, true);
}

TEST(NarrowPhaseRotated, ReportsASingleContactPointForCornerOnFace)
{
  auto flat = makeBox({ 0, 0, 0 });

  // 45 degrees about X, then atan(1/sqrt(2)) about Z, stands the cube on a corner: a body diagonal, of
  // length sqrt(3) from the center to a corner, runs straight down. The tip sits 0.1 below the flat
  // box's top, over the middle of it.
  const float centerY = 1.0f + std::sqrt(3.0f) - 0.1f;
  auto standing = makeBox({ 0, centerY, 0 }, { 45, 0, 35.26439f });

  const auto contact = collisions::findContact(*flat.collider, *standing.collider);
  ASSERT_TRUE(contact.has_value());

  EXPECT_NEAR(contact->depth(), 0.1f, tolerance);
  expectNear("normal", contact->normal(), { 0, -1, 0 });

  // No manifold: pointCount stays zero and contactPoints falls back to the one point.
  EXPECT_EQ(contact->pointCount, 0);
  ASSERT_EQ(contact->contactPoints().size(), 1u);
  expectNear("point", contact->contactPoints()[0], { 0, 0.9f, 0 });

  expectEveryPointInsideBoth(*contact, flat, standing);

  expectAgreement(flat, standing, true);
}

TEST(NarrowPhaseRotated, InventsNoPushForFacesThatOnlyTouch)
{
  auto lower = makeBox({ 0, 0, 0 });
  auto upper = makeBox({ 0, 2, 0 });

  // Exactly touching has no depth to resolve, so the full query either reports nothing or a contact
  // with none; what it must not do is invent a push.
  const auto touching = collisions::findContact(*lower.collider, *upper.collider);
  if (touching.has_value())
  {
    EXPECT_TRUE(isFinite(touching->minimumTranslationVector));
    EXPECT_LT(touching->depth(), tolerance);
  }

  // The controls on either side: a hair of overlap is a contact of that depth, a hair of gap is none.
  upper.object->getComponent<Transform>(ComponentType::transform)->setPosition({ 0, 1.99f, 0 });

  const auto overlapping = collisions::findContact(*lower.collider, *upper.collider);
  ASSERT_TRUE(overlapping.has_value());
  EXPECT_NEAR(overlapping->depth(), 0.01f, tolerance);

  upper.object->getComponent<Transform>(ComponentType::transform)->setPosition({ 0, 2.01f, 0 });

  EXPECT_FALSE(collisions::findContact(*lower.collider, *upper.collider).has_value());
  EXPECT_FALSE(collisions::intersects(*lower.collider, *upper.collider));
}

TEST(NarrowPhaseRotated, StaysFiniteWhereTwoEdgesCross)
{
  // The first box's top edge runs along x at y = sqrt(2); the second's bottom edge runs along z, 0.1
  // lower, over the same point. Only the pair of edges touches, 0.1 deep along y.
  const float edgeHeight = std::sqrt(2.0f);
  auto first = makeBox({ 0, 0, 0 }, { 45, 0, 0 });
  auto second = makeBox({ 0, 2.0f * edgeHeight - 0.1f, 0 }, { 0, 0, 45 });

  const auto contact = collisions::findContact(*first.collider, *second.collider);
  ASSERT_TRUE(contact.has_value());

  EXPECT_TRUE(isFinite(contact->minimumTranslationVector));
  EXPECT_TRUE(isFinite(contact->point));
  for (const auto& point : contact->contactPoints())
  {
    EXPECT_TRUE(isFinite(point));
  }

  EXPECT_NEAR(contact->depth(), 0.1f, 1e-2f);
  expectNear("normal", contact->normal(), { 0, -1, 0 }, 1e-2f);

  expectAgreement(first, second, true);

  // The control: lifted out of reach, nothing touches.
  second.object->getComponent<Transform>(ComponentType::transform)->setPosition({ 0, 2.0f * edgeHeight + 0.1f, 0 });
  second.position = { 0, 2.0f * edgeHeight + 0.1f, 0 };

  expectAgreement(first, second, false);
}

TEST(NarrowPhaseRotated, KeepsTheNormalPointingTheRightWayAtAHundredToOneScale)
{
  // A slab 100 times wider than the box on it: half extents (50, 0.5, 50) against (0.5, 0.5, 0.5). The
  // slab's top is y = 0.5 and the box's bottom y = 0.45, so the pair is 0.05 deep.
  auto slab = makeBox({ 0, 0, 0 }, { 0, 0, 0 }, { 50, 0.5f, 50 });
  auto small = makeBox({ 3, 0.95f, -2 }, { 0, 0, 0 }, { 0.5f, 0.5f, 0.5f });

  const auto contact = collisions::findContact(*slab.collider, *small.collider);
  ASSERT_TRUE(contact.has_value());

  EXPECT_TRUE(isFinite(contact->minimumTranslationVector));
  EXPECT_NEAR(contact->depth(), 0.05f, tolerance);
  expectNear("normal", contact->normal(), { 0, -1, 0 });

  const auto reversed = collisions::findContact(*small.collider, *slab.collider);
  ASSERT_TRUE(reversed.has_value());

  EXPECT_TRUE(isFinite(reversed->minimumTranslationVector));
  EXPECT_NEAR(reversed->depth(), 0.05f, tolerance);
  expectNear("reversed normal", reversed->normal(), { 0, 1, 0 });

  expectAgreement(slab, small, true);

  small.object->getComponent<Transform>(ComponentType::transform)->setPosition({ 3, 1.2f, -2 });
  small.position = { 3, 1.2f, -2 };

  expectAgreement(slab, small, false);
}
