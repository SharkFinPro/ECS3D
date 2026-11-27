#include <gtest/gtest.h>

#include "TerrainCursor.h"

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
  const auto onGround = strokeCursorHit(ground, { 32.0f, 10.0f, 30.0f }, down, 1000.0f);
  ASSERT_TRUE(onGround.has_value());
  EXPECT_NEAR(onGround->point.y, 10.0f, 1.0f);

  // Aiming above the horizon: the ray lands on the upright plane through the anchor that faces the camera.
  const gizmo::Ray up{ { 32.0f, 20.0f, 0.0f }, glm::normalize(glm::vec3(0.0f, 0.5f, 1.0f)) };
  const auto onPlane = strokeCursorHit(ground, { 32.0f, 10.0f, 30.0f }, up, 1000.0f);
  ASSERT_TRUE(onPlane.has_value());
  EXPECT_NEAR(onPlane->point.z, 30.0f, 1.0e-3f);
  EXPECT_NEAR(onPlane->point.y, 35.0f, 1.0e-3f);
  EXPECT_NEAR(onPlane->normal.z, -1.0f, 1.0e-6f);

  // Straight up: no plane faces the ray, so nothing.
  const gizmo::Ray straightUp{ { 32.0f, 20.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } };
  EXPECT_FALSE(strokeCursorHit(ground, { 32.0f, 10.0f, 30.0f }, straightUp, 1000.0f).has_value());
}
