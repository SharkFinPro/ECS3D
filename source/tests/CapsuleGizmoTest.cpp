#include <gtest/gtest.h>

#include "CapsuleGizmo.h"
#include "TestPrinters.h"
#include "TestScene.h"
#include "objects/Object.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/CapsuleCollider.h"

#include <glm/vec3.hpp>

namespace {
  constexpr float tolerance = 1e-4f;
}

TEST(CapsuleGizmo, AnUprightCapsuleGetsACylinderBetweenItsEndsAndASphereOnEach)
{
  const auto layout = layoutCapsuleGizmo({ 0, -0.5f, 0 }, { 0, 0.5f, 0 }, 0.5f);

  EXPECT_TRUE(layout.hasCylinder);
  fixtures::expectNear("cylinder position", layout.cylinderPosition, { 0, 0, 0 }, tolerance);
  fixtures::expectNear("cylinder scale", layout.cylinderScale, { 0.5f, 0.5f, 0.5f }, tolerance);
  fixtures::expectNear("start cap", layout.capPositions[0], { 0, -0.5f, 0 }, tolerance);
  fixtures::expectNear("end cap", layout.capPositions[1], { 0, 0.5f, 0 }, tolerance);
  fixtures::expectNear("cap scale", layout.capScale, glm::vec3(0.5f), tolerance);
}

TEST(CapsuleGizmo, ASegmentOffTheOriginPutsTheCylinderAtItsMidpointAndSizesItByHalfTheLength)
{
  const auto layout = layoutCapsuleGizmo({ 1, 2, 3 }, { 1, 6, 3 }, 0.25f);

  fixtures::expectNear("cylinder position", layout.cylinderPosition, { 1, 4, 3 }, tolerance);
  fixtures::expectNear("cylinder scale", layout.cylinderScale, { 0.25f, 2.0f, 0.25f }, tolerance);
  fixtures::expectNear("cap scale", layout.capScale, glm::vec3(0.25f), tolerance);
}

TEST(CapsuleGizmo, ACapsuleWithNoCoreSegmentIsJustTheTwoSpheres)
{
  const auto layout = layoutCapsuleGizmo({ 1, 1, 1 }, { 1, 1, 1 }, 0.5f);

  EXPECT_FALSE(layout.hasCylinder);
  fixtures::expectNear("start cap", layout.capPositions[0], { 1, 1, 1 }, tolerance);
  fixtures::expectNear("end cap", layout.capPositions[1], { 1, 1, 1 }, tolerance);

  // Positive control: any real segment length brings the cylinder back.
  EXPECT_TRUE(layoutCapsuleGizmo({ 0, 0, 0 }, { 0, 0.01f, 0 }, 0.5f).hasCylinder);
}

TEST(CapsuleGizmo, TheLayoutFollowsTheColliderTheRenderSystemReadsItFrom)
{
  fixtures::Scene scene;
  const auto object = fixtures::addObject(scene, "Capsule", { 5, 0, 0 }, { 2, 3, 2 });
  fixtures::transformOf(object)->setRotation({ 0, 0, 90 });
  const auto capsule = fixtures::addCapsuleCollider(object, 0.5f, 2.0f);

  // World radius 1 and half-height 3 leave a segment of half-length 2, lying along x.
  const auto [start, end] = capsule->getSegment();
  const auto layout = layoutCapsuleGizmo(start, end, capsule->getRadius());

  EXPECT_TRUE(layout.hasCylinder);
  fixtures::expectNear("cylinder position", layout.cylinderPosition, { 5, 0, 0 }, tolerance);
  fixtures::expectNear("cylinder scale", layout.cylinderScale, { 1.0f, 2.0f, 1.0f }, tolerance);
  fixtures::expectNear("start cap", layout.capPositions[0], { 7, 0, 0 }, tolerance);
  fixtures::expectNear("end cap", layout.capPositions[1], { 3, 0, 0 }, tolerance);
}
