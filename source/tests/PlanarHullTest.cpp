#include <gtest/gtest.h>

#include "PhysicsSystem.h"
#include "TestScene.h"
#include "collisions/PlanarHull.h"

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
  constexpr glm::vec3 up{ 0, 1, 0 };

  std::vector<glm::vec3> octagon(const float radius)
  {
    std::vector<glm::vec3> corners;
    for (int corner = 0; corner < 8; ++corner)
    {
      const float angle = glm::radians(45.0f * static_cast<float>(corner));
      corners.emplace_back(radius * std::cos(angle), 0.0f, radius * std::sin(angle));
    }

    return corners;
  }

  bool contains(const std::vector<glm::vec3>& points, const glm::vec3& point)
  {
    return std::ranges::any_of(points, [&point](const glm::vec3& candidate)
    {
      return glm::distance(candidate, point) < 1e-5f;
    });
  }
}

TEST(PlanarHull, DropsInteriorAndEdgePointsAndKeepsTheCorners)
{
  const std::vector<glm::vec3> points{
    { -1, 0, -1 }, { 1, 0, -1 }, { 1, 0, 1 }, { -1, 0, 1 }, { 0, 0, 0 }, { 0.5f, 0, -0.2f }, { 0, 0, -1 }
  };

  const auto outline = collisions::convexOutline(points, up);

  ASSERT_EQ(outline.size(), 4u);
  for (const auto& corner : { points[0], points[1], points[2], points[3] })
  {
    EXPECT_TRUE(contains(outline, corner));
  }
}

TEST(PlanarHull, WindsCounterClockwiseAboutTheNormal)
{
  const auto outline = collisions::convexOutline(octagon(1.0f), up);

  ASSERT_EQ(outline.size(), 8u);
  for (size_t i = 0; i < outline.size(); ++i)
  {
    const auto& a = outline[i];
    const auto& b = outline[(i + 1) % outline.size()];
    const auto& c = outline[(i + 2) % outline.size()];
    EXPECT_GT(glm::dot(glm::cross(b - a, c - b), up), 0.0f) << i;
  }
}

TEST(PlanarHull, ReturnsFewerThanThreePointsAsTheyAre)
{
  const std::vector<glm::vec3> two{ { 0, 0, 0 }, { 1, 0, 0 } };

  EXPECT_EQ(collisions::convexOutline(two, up), two);
  EXPECT_TRUE(collisions::convexOutline({}, up).empty());
}

TEST(PlanarHull, CollinearPointsReduceToTheirEnds)
{
  const std::vector<glm::vec3> line{ { 0, 0, 0 }, { 1, 0, 0 }, { 2, 0, 0 }, { 3, 0, 0 } };

  const auto outline = collisions::convexOutline(line, up);

  EXPECT_TRUE(contains(outline, line.front()));
  EXPECT_TRUE(contains(outline, line.back()));
  EXPECT_LE(outline.size(), 3u);
}

TEST(PhysicsSupportPolygon, EightPointsAreOneSupportRegion)
{
  const auto corners = octagon(1.0f);

  const glm::vec3 insideTheLastCorners{ -0.5f, 1.0f, 0.1f };
  const auto support = PhysicsSystem::supportPoint(insideTheLastCorners, up, corners);
  fixtures::expectNear("inside, over the later corners", support, { -0.5f, 0.0f, 0.1f }, 1e-5f);

  const auto outside = PhysicsSystem::supportPoint({ 3, 1, 0 }, up, corners);
  fixtures::expectNear("outside, clamped to the nearest corner", outside, { 1, 0, 0 }, 1e-5f);

  const auto between = PhysicsSystem::supportPoint({ 2, 1, 0.3f }, up, corners);
  EXPECT_LE(glm::length(glm::vec2(between.x, between.z)), 1.0f + 1e-5f);
  EXPECT_GT(between.x, 0.9f);
}
