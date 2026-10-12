#include <gtest/gtest.h>

#include "TestPrinters.h"
#include "TestScene.h"
#include "collisions/ContactManifold.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace {
  using fixtures::expectNear;

  constexpr float tolerance = 1e-3f;

  std::vector<glm::vec3> unitCubeVertices()
  {
    std::vector<glm::vec3> vertices;

    for (uint32_t i = 0; i < 8; ++i)
    {
      vertices.emplace_back((i & 1u) ? 1.0f : -1.0f, (i & 2u) ? 1.0f : -1.0f, (i & 4u) ? 1.0f : -1.0f);
    }

    return vertices;
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
  const auto cube = unitCubeVertices();

  EXPECT_EQ(collisions::touchingFeature(cube, { 0, 1, 0 }).size(), 4u);
  EXPECT_EQ(collisions::touchingFeature(cube, { 1, 1, 0 }).size(), 2u);
  EXPECT_EQ(collisions::touchingFeature(cube, { 1, 1, 1 }).size(), 1u);

  for (const auto& vertex : collisions::touchingFeature(cube, { 0, 1, 0 }))
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

TEST(ContactManifold, ACrossedIncidentFaceIsOrderedBeforeItIsClipped)
{
  // The same squares with their corners listed in a crossed order, as a box's touching vertices come out.
  // Clipped as listed, the incident face is a bow tie and loses its overlap.
  const std::vector<glm::vec3> lowerTop{ { -1, 1, -1 }, { 1, 1, 1 }, { 1, 1, -1 }, { -1, 1, 1 } };
  const std::vector<glm::vec3> upperBottom{ { 0, 0.9f, -1 }, { 2, 0.9f, 1 }, { 2, 0.9f, -1 }, { 0, 0.9f, 1 } };

  const auto manifold = collisions::clipManifold(lowerTop, upperBottom, { 0, 1, 0 }, 4);

  ASSERT_EQ(manifold.size(), 4u);
  expectPointsInsideSquare(manifold, 0.0f, 1.0f, -1.0f, 1.0f);
  EXPECT_TRUE(hasPointNear(manifold, { 0.0f, 1.0f, -1.0f }));
  EXPECT_TRUE(hasPointNear(manifold, { 1.0f, 1.0f, 1.0f }));
}

TEST(ContactManifold, FacesThatDoNotOverlapFallBackToTheIncidentFeature)
{
  const auto lowerTop = squareAt(1.0f, -1.0f, 1.0f, 1.0f);
  const auto farAway = squareAt(0.9f, 5.0f, 7.0f, 1.0f);

  const auto manifold = collisions::clipManifold(lowerTop, farAway, { 0, 1, 0 }, 4);

  EXPECT_FALSE(manifold.empty());

  // The control: faces that do overlap clip to their shared footprint.
  EXPECT_EQ(collisions::clipManifold(lowerTop, squareAt(0.9f, 0.0f, 2.0f, 1.0f), { 0, 1, 0 }, 4).size(), 4u);
}

TEST(ContactManifold, ASinglePointOnAFaceLandsOnThatFace)
{
  const auto faceTop = squareAt(-0.1f, -1.0f, 1.0f, 1.0f);
  const std::vector<glm::vec3> corner{ { 0.2f, 0.0f, 0.3f } };

  const auto manifold = collisions::clipManifold(corner, faceTop, { 0, -1, 0 }, 4);

  ASSERT_EQ(manifold.size(), 1u);
  expectNear("point", manifold[0], { 0.2f, -0.1f, 0.3f }, tolerance);
}

TEST(ContactManifold, TwoCrossedEdgesMeetAtOnePointMidwayBetweenThem)
{
  const std::vector<glm::vec3> upperEdge{ { -1, 0, 0 }, { 1, 0, 0 } };
  const std::vector<glm::vec3> lowerEdge{ { 0, -0.1f, -1 }, { 0, -0.1f, 1 } };

  const auto manifold = collisions::clipManifold(upperEdge, lowerEdge, { 0, -1, 0 }, 4);

  ASSERT_EQ(manifold.size(), 1u);
  expectNear("point", manifold[0], { 0, -0.05f, 0 }, tolerance);
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
