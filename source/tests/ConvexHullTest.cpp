#include <gtest/gtest.h>

#include "TestPrinters.h"
#include "objects/components/collisions/ConvexHull.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <utility>
#include <vector>

namespace {
  std::vector<glm::vec3> cubeCorners()
  {
    std::vector<glm::vec3> corners;

    for (const float x : { -1.0f, 1.0f })
    {
      for (const float y : { -1.0f, 1.0f })
      {
        for (const float z : { -1.0f, 1.0f })
        {
          corners.emplace_back(x, y, z);
        }
      }
    }

    return corners;
  }

  glm::vec3 centroidOf(const std::vector<glm::vec3>& points)
  {
    glm::vec3 sum{ 0 };

    for (const auto& point : points)
    {
      sum += point;
    }

    return sum / static_cast<float>(points.size());
  }

  glm::vec3 triangleNormal(const ConvexHull& hull, const size_t triangle)
  {
    const glm::vec3& a = hull.vertices[hull.indices[triangle * 3]];
    const glm::vec3& b = hull.vertices[hull.indices[triangle * 3 + 1]];
    const glm::vec3& c = hull.vertices[hull.indices[triangle * 3 + 2]];

    return glm::normalize(glm::cross(b - a, c - a));
  }

  // Every triangle faces away from the hull's centroid.
  void expectOutwardWinding(const ConvexHull& hull)
  {
    const glm::vec3 center = centroidOf(hull.vertices);

    for (size_t triangle = 0; triangle < hull.indices.size() / 3; ++triangle)
    {
      const glm::vec3& a = hull.vertices[hull.indices[triangle * 3]];
      const glm::vec3 normal = triangleNormal(hull, triangle);

      EXPECT_GT(glm::dot(normal, a - center), 0.0f) << "triangle " << triangle;
    }
  }

  // How far the point sits outside the hull: the largest signed distance to any face plane, so a point
  // inside or on the hull gives zero or less.
  float outsideDistance(const ConvexHull& hull, const glm::vec3& point)
  {
    float worst = std::numeric_limits<float>::lowest();

    for (size_t triangle = 0; triangle < hull.indices.size() / 3; ++triangle)
    {
      const glm::vec3& a = hull.vertices[hull.indices[triangle * 3]];
      worst = std::max(worst, glm::dot(triangleNormal(hull, triangle), point - a));
    }

    return worst;
  }

  size_t uniqueTriangleEdges(const ConvexHull& hull)
  {
    std::set<std::pair<uint32_t, uint32_t>> edges;

    for (size_t i = 0; i < hull.indices.size(); i += 3)
    {
      for (size_t edge = 0; edge < 3; ++edge)
      {
        const uint32_t from = hull.indices[i + edge];
        const uint32_t to = hull.indices[i + (edge + 1) % 3];
        edges.insert({ std::min(from, to), std::max(from, to) });
      }
    }

    return edges.size();
  }

  // A deterministic stand-in for random: a small linear congruential generator in [-1, 1).
  struct Lcg
  {
    uint32_t state = 12345u;

    float next()
    {
      state = state * 1664525u + 1013904223u;
      return static_cast<float>(state >> 8) / static_cast<float>(1u << 23) - 1.0f;
    }
  };

  std::vector<glm::vec3> fibonacciSphere(const size_t count)
  {
    std::vector<glm::vec3> points;
    constexpr double goldenAngle = 2.39996322972865332;

    for (size_t i = 0; i < count; ++i)
    {
      const double y = 1.0 - 2.0 * (static_cast<double>(i) + 0.5) / static_cast<double>(count);
      const double radius = std::sqrt(1.0 - y * y);
      const double theta = goldenAngle * static_cast<double>(i);

      points.emplace_back(static_cast<float>(std::cos(theta) * radius), static_cast<float>(y),
                          static_cast<float>(std::sin(theta) * radius));
    }

    return points;
  }
}

TEST(ConvexHull, ACubeWithInteriorAndDuplicatePointsKeepsOnlyItsEightCorners)
{
  auto points = cubeCorners();
  points.emplace_back(0.0f, 0.0f, 0.0f);
  points.emplace_back(0.5f, 0.5f, 0.5f);
  points.emplace_back(0.9f, 0.0f, -0.3f);
  points.emplace_back(1.0f, 1.0f, 1.0f);
  points.emplace_back(-1.0f, 1.0f, -1.0f);

  const auto hull = buildConvexHull(points);

  ASSERT_TRUE(hull.has_value());
  EXPECT_EQ(hull->vertices.size(), 8u);
  EXPECT_EQ(hull->indices.size(), 36u);

  for (const auto& vertex : hull->vertices)
  {
    EXPECT_FLOAT_EQ(std::fabs(vertex.x), 1.0f);
    EXPECT_FLOAT_EQ(std::fabs(vertex.y), 1.0f);
    EXPECT_FLOAT_EQ(std::fabs(vertex.z), 1.0f);
  }

  expectOutwardWinding(*hull);

  for (const auto& point : points)
  {
    EXPECT_LE(outsideDistance(*hull, point), 1e-5f);
  }
}

TEST(ConvexHull, ATetrahedronIsFourVerticesAndFourOutwardTriangles)
{
  const std::vector<glm::vec3> points{ { 0, 0, 0 }, { 2, 0, 0 }, { 0, 2, 0 }, { 0, 0, 2 } };

  const auto hull = buildConvexHull(points);

  ASSERT_TRUE(hull.has_value());
  EXPECT_EQ(hull->vertices.size(), 4u);
  EXPECT_EQ(hull->indices.size(), 12u);
  expectOutwardWinding(*hull);
}

TEST(ConvexHull, EveryIndexNamesAVertexAndEveryVertexIsUsed)
{
  const auto hull = buildConvexHull(cubeCorners());
  ASSERT_TRUE(hull.has_value());

  std::set<uint32_t> used;
  for (const uint32_t index : hull->indices)
  {
    EXPECT_LT(index, hull->vertices.size());
    used.insert(index);
  }

  EXPECT_EQ(used.size(), hull->vertices.size());
}

TEST(ConvexHull, RefusesInputThatHasNoVolume)
{
  // The control: a real solid from the same kind of input builds.
  EXPECT_TRUE(buildConvexHull(cubeCorners()).has_value());

  const std::vector<glm::vec3> coplanar{ { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 0.5f, 0.5f, 0 } };
  EXPECT_FALSE(buildConvexHull(coplanar).has_value());

  const std::vector<glm::vec3> nearlyCoplanar{ { 0, 0, 0 }, { 1, 0, 1e-9f }, { 0, 1, 0 }, { 1, 1, -1e-9f } };
  EXPECT_FALSE(buildConvexHull(nearlyCoplanar).has_value());

  const std::vector<glm::vec3> collinear{ { 0, 0, 0 }, { 1, 1, 1 }, { 2, 2, 2 }, { 3, 3, 3 }, { -1, -1, -1 } };
  EXPECT_FALSE(buildConvexHull(collinear).has_value());

  const std::vector<glm::vec3> sameSpot(10, glm::vec3(2.0f, 3.0f, 4.0f));
  EXPECT_FALSE(buildConvexHull(sameSpot).has_value());
}

TEST(ConvexHull, RefusesTooFewPoints)
{
  const std::vector<glm::vec3> three{ { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };

  EXPECT_FALSE(buildConvexHull(three).has_value());
  EXPECT_FALSE(buildConvexHull(std::vector<glm::vec3>{}).has_value());

  auto four = three;
  four.emplace_back(0.0f, 0.0f, 1.0f);
  EXPECT_TRUE(buildConvexHull(four).has_value());
}

TEST(ConvexHull, RefusesNonFiniteInput)
{
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();

  auto withNan = cubeCorners();
  withNan.emplace_back(nan, 0.0f, 0.0f);
  EXPECT_FALSE(buildConvexHull(withNan).has_value());

  auto withInf = cubeCorners();
  withInf.emplace_back(0.0f, inf, 0.0f);
  EXPECT_FALSE(buildConvexHull(withInf).has_value());

  EXPECT_TRUE(buildConvexHull(cubeCorners()).has_value());
}

TEST(ConvexHull, TheSameInputGivesByteIdenticalOutput)
{
  Lcg random;
  std::vector<glm::vec3> points;
  for (int i = 0; i < 500; ++i)
  {
    points.emplace_back(random.next(), random.next(), random.next());
  }

  const auto first = buildConvexHull(points);
  const auto second = buildConvexHull(points);

  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(first->indices, second->indices);
  ASSERT_EQ(first->vertices.size(), second->vertices.size());

  // Exact on purpose: a nondeterministic build is what this is here to catch.
  for (size_t i = 0; i < first->vertices.size(); ++i)
  {
    EXPECT_EQ(first->vertices[i], second->vertices[i]) << "vertex " << i;
  }
}

TEST(ConvexHull, ARandomCloudGivesAClosedConvexHullAroundEveryPoint)
{
  Lcg random;
  std::vector<glm::vec3> points;
  for (int i = 0; i < 300; ++i)
  {
    points.emplace_back(random.next() * 3.0f, random.next(), random.next() * 0.5f);
  }

  const auto hull = buildConvexHull(points);
  ASSERT_TRUE(hull.has_value());

  expectOutwardWinding(*hull);

  // A closed triangle mesh has V - E + F = 2 and every edge shared by exactly two triangles.
  const size_t triangles = hull->indices.size() / 3;
  EXPECT_EQ(static_cast<long>(hull->vertices.size()) - static_cast<long>(uniqueTriangleEdges(*hull))
              + static_cast<long>(triangles),
            2);
  EXPECT_EQ(uniqueTriangleEdges(*hull) * 2, hull->indices.size());

  for (const auto& point : points)
  {
    EXPECT_LE(outsideDistance(*hull, point), 1e-4f);
  }
}

TEST(ConvexHull, ACloudWithMoreThanTheVertexLimitIsReducedToFit)
{
  const auto points = fibonacciSphere(2000);

  // Every point of a sphere is a hull vertex, so only the reduction can bring this under the limit.
  const auto hull = buildConvexHull(points);

  ASSERT_TRUE(hull.has_value());
  EXPECT_LE(hull->vertices.size(), maxConvexHullVertices);
  EXPECT_GT(hull->vertices.size(), 100u);
  EXPECT_TRUE(isValidConvexHull(*hull));
  expectOutwardWinding(*hull);

  for (const auto& point : points)
  {
    EXPECT_LE(outsideDistance(*hull, point), 0.05f);
  }

  // A cloud that already fits is not reduced.
  const auto small = buildConvexHull(fibonacciSphere(200));
  ASSERT_TRUE(small.has_value());
  EXPECT_GT(small->vertices.size(), 150u);
}

TEST(ConvexHull, ReductionIsDeterministic)
{
  const auto points = fibonacciSphere(1000);

  const auto first = buildConvexHull(points);
  const auto second = buildConvexHull(points);

  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(first->indices, second->indices);
  EXPECT_EQ(first->vertices, second->vertices);
}

TEST(ConvexHull, ACubeDrawsTwelveEdgesNotEighteen)
{
  const auto hull = buildConvexHull(cubeCorners());
  ASSERT_TRUE(hull.has_value());

  // The positive control: the raw triangle mesh has the six face diagonals as well.
  EXPECT_EQ(uniqueTriangleEdges(*hull), 18u);
  EXPECT_EQ(hullEdges(*hull).size(), 12u);

  EXPECT_EQ(uniqueTriangleEdges(makeUnitCubeHull()), 18u);
  EXPECT_EQ(hullEdges(makeUnitCubeHull()).size(), 12u);
}

TEST(ConvexHull, ATetrahedronDrawsAllSixEdgesAsAscendingPairs)
{
  const std::vector<glm::vec3> points{ { 0, 0, 0 }, { 2, 0, 0 }, { 0, 2, 0 }, { 0, 0, 2 } };
  const auto hull = buildConvexHull(points);
  ASSERT_TRUE(hull.has_value());

  const auto edges = hullEdges(*hull);

  EXPECT_EQ(edges.size(), 6u);
  EXPECT_TRUE(std::ranges::is_sorted(edges));

  for (const auto& [from, to] : edges)
  {
    EXPECT_LT(from, to);
    EXPECT_LT(to, hull->vertices.size());
  }
}

TEST(ConvexHull, TheUnitCubeIsAValidOutwardHull)
{
  const auto cube = makeUnitCubeHull();

  EXPECT_TRUE(isValidConvexHull(cube));
  EXPECT_EQ(cube.vertices.size(), 8u);
  EXPECT_EQ(cube.indices.size(), 36u);
  expectOutwardWinding(cube);
}

TEST(ConvexHull, ValidationRejectsWhatTheColliderCouldNotUse)
{
  EXPECT_TRUE(isValidConvexHull(makeUnitCubeHull()));

  auto tooFewVertices = makeUnitCubeHull();
  tooFewVertices.vertices.resize(3);
  tooFewVertices.indices = { 0, 1, 2 };
  EXPECT_FALSE(isValidConvexHull(tooFewVertices));

  auto noTriangles = makeUnitCubeHull();
  noTriangles.indices.clear();
  EXPECT_FALSE(isValidConvexHull(noTriangles));

  auto ragged = makeUnitCubeHull();
  ragged.indices.pop_back();
  EXPECT_FALSE(isValidConvexHull(ragged));

  auto outOfRange = makeUnitCubeHull();
  outOfRange.indices[5] = 8;
  EXPECT_FALSE(isValidConvexHull(outOfRange));

  auto nonFinite = makeUnitCubeHull();
  nonFinite.vertices[2].y = std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(isValidConvexHull(nonFinite));

  auto tooManyVertices = makeUnitCubeHull();
  tooManyVertices.vertices.assign(maxConvexHullVertices + 1, glm::vec3(1.0f));
  EXPECT_FALSE(isValidConvexHull(tooManyVertices));
}
