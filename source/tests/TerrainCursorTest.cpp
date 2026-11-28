#include <gtest/gtest.h>

#include "TerrainCursor.h"

#include <terrain/TerrainBrush.h>
#include <terrain/TerrainGenerators.h>
#include <terrain/TerrainOps.h>
#include <terrain/TerrainRaycast.h>
#include <terrain/TerrainVolume.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <cstddef>
#include <limits>
#include <optional>
#include <vector>

namespace {
  void expectRing(const glm::vec3& center, const glm::vec3& normal, const float radius)
  {
    const std::vector<glm::vec3> ring = brushRing(center, normal, radius);
    const glm::vec3 unit = glm::normalize(normal);

    ASSERT_EQ(ring.size(), brushRingSegments);

    for (size_t index = 0; index < ring.size(); ++index)
    {
      const glm::vec3 offset = ring[index] - center;
      EXPECT_NEAR(glm::length(offset), radius, 1.0e-4f * radius);
      EXPECT_NEAR(glm::dot(offset, unit), 0.0f, 1.0e-4f * radius);

      const glm::vec3 next = ring[(index + 1) % ring.size()];
      EXPECT_GT(glm::length(next - ring[index]), 0.0f);
    }
  }

  TerrainFrame rotatedFrame()
  {
    TerrainFrame frame;
    frame.position = { 100.0f, -20.0f, 55.0f };
    frame.orientation = glm::normalize(glm::quat(glm::radians(glm::vec3(20.0f, 70.0f, -35.0f))));
    return frame;
  }

  void expectNear(const glm::vec3& a, const glm::vec3& b, const float tolerance)
  {
    EXPECT_NEAR(a.x, b.x, tolerance);
    EXPECT_NEAR(a.y, b.y, tolerance);
    EXPECT_NEAR(a.z, b.z, tolerance);
  }
}

TEST(BrushRingTest, PointsLieAtTheRadiusPerpendicularToAnUpNormal)
{
  expectRing({ 5.0f, 3.0f, -2.0f }, { 0.0f, 1.0f, 0.0f }, 7.0f);
}

TEST(BrushRingTest, PointsLieAtTheRadiusPerpendicularToAWallNormal)
{
  expectRing({ 5.0f, 3.0f, -2.0f }, { 1.0f, 0.0f, 0.0f }, 7.0f);
  expectRing({ -4.0f, 9.0f, 1.0f }, { 0.0f, 0.0f, -1.0f }, 3.5f);
}

TEST(BrushRingTest, PointsLieAtTheRadiusPerpendicularToACeilingNormal)
{
  expectRing({ 5.0f, 3.0f, -2.0f }, { 0.0f, -1.0f, 0.0f }, 7.0f);
}

TEST(BrushRingTest, ASlantedAndUnnormalizedNormalWorks)
{
  expectRing({ 0.0f, 0.0f, 0.0f }, { 2.0f, 4.0f, -3.0f }, 12.0f);
}

TEST(BrushRingTest, ADegenerateNormalFallsBackToUp)
{
  const glm::vec3 center{ 1.0f, 6.0f, 2.0f };
  const std::vector<glm::vec3> normals = { glm::vec3(0.0f),
                                           glm::vec3(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f) };

  for (const glm::vec3& normal : normals)
  {
    const std::vector<glm::vec3> ring = brushRing(center, normal, 4.0f);
    ASSERT_EQ(ring.size(), brushRingSegments);

    for (const glm::vec3& point : ring)
    {
      EXPECT_NEAR(point.y, center.y, 1.0e-4f);
      EXPECT_NEAR(glm::length(point - center), 4.0f, 1.0e-4f);
    }
  }
}

TEST(TerrainFrameTest, PointsAndDirectionsRoundTripThroughARotatedTranslatedFrame)
{
  const TerrainFrame frame = rotatedFrame();
  const glm::vec3 world{ 31.0f, 7.0f, -12.0f };

  expectNear(frame.pointToWorld(frame.pointToLocal(world)), world, 1.0e-3f);
  expectNear(frame.directionToWorld(frame.directionToLocal(world)), world, 1.0e-3f);
  expectNear(frame.pointToWorld({ 0.0f, 0.0f, 0.0f }), frame.position, 1.0e-6f);
  EXPECT_NEAR(glm::length(frame.directionToLocal({ 0.0f, 3.0f, 4.0f })), 5.0f, 1.0e-4f);
}

TEST(TerrainFrameTest, AWorldRayHitsTheSamePointOfARotatedTerrainAsALocalOne)
{
  terrain::TerrainVolume volume(2.5f);
  terrain::commit(volume, terrain::fillFlat(volume, { 64, 64 }, 10.0f, 20.0f, 1));

  const TerrainFrame frame = rotatedFrame();
  const glm::vec3 above = frame.pointToWorld({ 40.0f, 60.0f, 40.0f });
  const gizmo::Ray worldRay{ above, frame.directionToWorld({ 0.0f, -1.0f, 0.0f }) };

  const gizmo::Ray local = rayToLocal(frame, worldRay);
  expectNear(local.origin, { 40.0f, 60.0f, 40.0f }, 1.0e-3f);
  expectNear(local.direction, { 0.0f, -1.0f, 0.0f }, 1.0e-4f);

  const std::optional<terrain::RayHit> hit = terrain::raycast(volume, local.origin, local.direction, 500.0f);
  ASSERT_TRUE(hit.has_value());
  EXPECT_NEAR(hit->point.x, 40.0f, 0.05f);
  EXPECT_NEAR(hit->point.z, 40.0f, 0.05f);

  const terrain::RayHit world = hitToWorld(frame, *hit);
  expectNear(world.point, frame.pointToWorld(hit->point), 1.0e-4f);
  EXPECT_NEAR(glm::dot(glm::normalize(world.normal), frame.directionToWorld({ 0.0f, 1.0f, 0.0f })), 1.0f, 1.0e-3f);
}

TEST(StrokeCursorHitTest, AimsAtTheStartingSurfaceAndFallsBackToAnUprightPlane)
{
  terrain::TerrainVolume ground(1.0f);
  terrain::commit(ground, terrain::fillFlat(ground, { 64, 64 }, 10.0f, 4.0f, 1));

  // Looking down at the ground: the hit is on it, where the stroke's material would not be.
  const gizmo::Ray down{ { 32.0f, 40.0f, 20.0f }, glm::normalize(glm::vec3(0.0f, -1.0f, 0.5f)) };
  const auto onGround = strokeCursorHit(ground, ground, { 32.0f, 10.0f, 35.0f }, down, 1000.0f, 4.0f);
  ASSERT_TRUE(onGround.has_value());
  EXPECT_NEAR(onGround->point.y, 10.0f, 1.0f);

  // Aiming above the horizon: the ray lands on the upright plane through the anchor that faces the camera.
  const gizmo::Ray up{ { 32.0f, 20.0f, 0.0f }, glm::normalize(glm::vec3(0.0f, 0.5f, 1.0f)) };
  const auto onPlane = strokeCursorHit(ground, ground, { 32.0f, 10.0f, 30.0f }, up, 1000.0f, 4.0f);
  ASSERT_TRUE(onPlane.has_value());
  EXPECT_NEAR(onPlane->point.z, 30.0f, 1.0e-3f);
  EXPECT_NEAR(onPlane->point.y, 35.0f, 1.0e-3f);
  EXPECT_NEAR(onPlane->normal.z, -1.0f, 1.0e-6f);

  // Straight up: no plane faces the ray, so nothing.
  const gizmo::Ray straightUp{ { 32.0f, 20.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } };
  EXPECT_FALSE(strokeCursorHit(ground, ground, { 32.0f, 10.0f, 30.0f }, straightUp, 1000.0f, 4.0f).has_value());

  // Drawing a column down toward ground far behind it: the plane through the column wins over the distant ground.
  const gizmo::Ray downAcross{ { 32.0f, 30.0f, 0.0f }, glm::normalize(glm::vec3(0.0f, -0.5f, 1.0f)) };
  ASSERT_TRUE(terrain::raycast(ground, downAcross.origin, downAcross.direction, 1000.0f).has_value());
  const auto onColumn = strokeCursorHit(ground, ground, { 32.0f, 20.0f, 20.0f }, downAcross, 1000.0f, 4.0f);
  ASSERT_TRUE(onColumn.has_value());
  EXPECT_NEAR(onColumn->point.z, 20.0f, 1.0e-3f);
  EXPECT_NEAR(onColumn->point.y, 20.0f, 1.0e-3f);
}

TEST(StrokeCursorHitTest, OverDrawnMaterialTheStrokeStaysNearItInsteadOfTheGroundBehind)
{
  terrain::TerrainVolume surface(1.0f);
  terrain::commit(surface, terrain::fillFlat(surface, { 64, 64 }, 10.0f, 4.0f, 1));

  terrain::TerrainVolume live(1.0f);
  terrain::commit(live, terrain::fillFlat(live, { 64, 64 }, 10.0f, 4.0f, 1));
  terrain::BrushOp blob;
  blob.kind = terrain::BrushOp::Kind::add;
  blob.center = { 32.0f, 22.0f, 10.0f };
  blob.radius = 6.0f;
  blob.strength = 1.0f;
  blob.falloff = 0.0f;
  blob.material = 1;
  terrain::commit(live, terrain::applyBrush(live, blob));

  const gizmo::Ray ray{ { 32.0f, 30.0f, 0.0f }, glm::normalize(glm::vec3(0.0f, -0.5f, 1.0f)) };
  const auto farGround = terrain::raycast(surface, ray.origin, ray.direction, 1000.0f);
  ASSERT_TRUE(farGround.has_value());

  const auto hit = strokeCursorHit(surface, live, blob.center, ray, 1000.0f, 12.0f);
  ASSERT_TRUE(hit.has_value());
  EXPECT_LT(glm::length(hit->point - blob.center), 12.0f);
  EXPECT_GT(glm::length(hit->point - farGround->point), 12.0f);
}

TEST(BrushDiskHitTest, FindsTerrainUnderTheBrushThatTheCursorMisses)
{
  terrain::TerrainVolume pillar(1.0f);
  terrain::commit(pillar, terrain::fillFlat(pillar, { 4, 4 }, 10.0f, 4.0f, 1));

  const gizmo::Ray ray{ { 10.0f, 5.0f, -20.0f }, { 0.0f, 0.0f, 1.0f } };
  ASSERT_FALSE(terrain::raycast(pillar, ray.origin, ray.direction, 1000.0f).has_value());

  const auto hit = brushDiskHit(pillar, ray, 8.0f, 1000.0f);
  ASSERT_TRUE(hit.has_value());
  EXPECT_NEAR(hit->point.x, 10.0f, 1.0e-3f);
  EXPECT_NEAR(hit->point.y, 5.0f, 1.0e-3f);
  EXPECT_LT(hit->point.z, 5.0f);

  EXPECT_FALSE(brushDiskHit(pillar, ray, 2.0f, 1000.0f).has_value());
}

TEST(BrushDiskHitTest, TheCursorsOwnHitWinsOverNearerTerrainAtTheRim)
{
  terrain::TerrainVolume ground(1.0f);
  terrain::commit(ground, terrain::fillFlat(ground, { 64, 64 }, 10.0f, 4.0f, 1));
  terrain::BrushOp bump;
  bump.kind = terrain::BrushOp::Kind::add;
  bump.center = { 37.0f, 18.0f, 32.0f };
  bump.radius = 3.0f;
  bump.strength = 1.0f;
  bump.falloff = 0.0f;
  bump.material = 1;
  terrain::commit(ground, terrain::applyBrush(ground, bump));

  const gizmo::Ray down{ { 32.0f, 40.0f, 32.0f }, { 0.0f, -1.0f, 0.0f } };
  const auto hit = brushDiskHit(ground, down, 6.0f, 1000.0f);
  ASSERT_TRUE(hit.has_value());
  EXPECT_NEAR(hit->point.y, 10.0f, 1.0f);
}

TEST(StrokeAimTest, APaintStrokeFollowsTheGroundAcrossAJumpThatSendsOtherStrokesToTheUprightPlane)
{
  terrain::TerrainVolume ground(2.5f);
  terrain::commit(ground, terrain::fillFlat(ground, { 64, 64 }, 10.0f, 20.0f, 1));

  const glm::vec3 anchor{ 40.0f, 10.0f, 40.0f };
  const glm::vec3 target{ 40.0f, 10.0f, 100.0f };
  const glm::vec3 eye{ 40.0f, 60.0f, 0.0f };
  const gizmo::Ray ray{ eye, glm::normalize(target - eye) };
  constexpr float maxJump = 20.0f;

  // The cursor moved three jump limits in one frame, so the raising strokes' aim lands on the plane over the anchor,
  // well above the ground, where the paint brush reaches no surface sample.
  const auto raising = strokeAim(ground, ground, anchor, ray, 1000.0f, maxJump, true);
  ASSERT_TRUE(raising.has_value());
  EXPECT_GT(raising->point.y, 30.0f);
  EXPECT_NEAR(raising->point.z, anchor.z, 1.0e-3f);

  const auto painting = strokeAim(ground, ground, anchor, ray, 1000.0f, maxJump, false);
  ASSERT_TRUE(painting.has_value());
  EXPECT_NEAR(painting->point.x, target.x, 0.5f);
  EXPECT_NEAR(painting->point.y, target.y, 0.5f);
  EXPECT_NEAR(painting->point.z, target.z, 0.5f);

  // Over the sky there is no ground, so a paint stroke has no hit instead of a plane point.
  const gizmo::Ray sky{ eye, glm::normalize(glm::vec3(0.0f, 0.5f, 1.0f)) };
  EXPECT_FALSE(strokeAim(ground, ground, anchor, sky, 1000.0f, maxJump, false).has_value());
  EXPECT_TRUE(strokeAim(ground, ground, anchor, sky, 1000.0f, maxJump, true).has_value());
}
