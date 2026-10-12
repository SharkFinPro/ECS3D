#include <gtest/gtest.h>

#include "TestPrinters.h"
#include "objects/components/collisions/ConvexHull.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
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

  glm::dvec3 centroidOf(const std::vector<glm::vec3>& points)
  {
    glm::dvec3 sum{ 0.0 };

    for (const auto& point : points)
    {
      sum += glm::dvec3(point);
    }

    return sum / static_cast<double>(points.size());
  }

  // In double: the normal of a sliver triangle in a thin hull loses too much to float arithmetic.
  glm::dvec3 triangleNormal(const ConvexHull& hull, const size_t triangle)
  {
    const glm::dvec3 a(hull.vertices[hull.indices[triangle * 3]]);
    const glm::dvec3 b(hull.vertices[hull.indices[triangle * 3 + 1]]);
    const glm::dvec3 c(hull.vertices[hull.indices[triangle * 3 + 2]]);

    return glm::normalize(glm::cross(b - a, c - a));
  }

  // Every triangle faces away from the hull's centroid.
  void expectOutwardWinding(const ConvexHull& hull)
  {
    const glm::dvec3 center = centroidOf(hull.vertices);

    for (size_t triangle = 0; triangle < hull.indices.size() / 3; ++triangle)
    {
      const glm::dvec3 a(hull.vertices[hull.indices[triangle * 3]]);
      const glm::dvec3 normal = triangleNormal(hull, triangle);

      EXPECT_GT(glm::dot(normal, a - center), 0.0) << "triangle " << triangle;
    }
  }

  // How far the point sits outside the hull: the largest signed distance to any face plane, so a point
  // inside or on the hull gives zero or less.
  double outsideDistance(const ConvexHull& hull, const glm::vec3& point)
  {
    double worst = std::numeric_limits<double>::lowest();

    for (size_t triangle = 0; triangle < hull.indices.size() / 3; ++triangle)
    {
      const glm::dvec3 a(hull.vertices[hull.indices[triangle * 3]]);
      worst = std::max(worst, glm::dot(triangleNormal(hull, triangle), glm::dvec3(point) - a));
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

  // Closed (every edge shared by exactly two triangles, V - E + F = 2), outward and convex around every point.
  void expectSound(const ConvexHull& hull, const std::vector<glm::vec3>& points, const double tolerance)
  {
    expectOutwardWinding(hull);

    const size_t edges = uniqueTriangleEdges(hull);
    EXPECT_EQ(edges * 2, hull.indices.size());
    EXPECT_EQ(static_cast<long>(hull.vertices.size()) - static_cast<long>(edges)
                + static_cast<long>(hull.indices.size() / 3),
              2);

    for (const auto& point : points)
    {
      EXPECT_LE(outsideDistance(hull, point), tolerance);
    }

    for (const auto& vertex : hull.vertices)
    {
      EXPECT_LE(outsideDistance(hull, vertex), tolerance);
    }
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

  std::vector<glm::vec3> randomCloud(const size_t count, const glm::vec3& scale)
  {
    Lcg random;
    std::vector<glm::vec3> points;

    for (size_t i = 0; i < count; ++i)
    {
      const float x = random.next() * scale.x;
      const float y = random.next() * scale.y;
      const float z = random.next() * scale.z;
      points.emplace_back(x, y, z);
    }

    return points;
  }

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

  // The integer lattice points of [low, high]^3 that satisfy keep.
  template <typename Keep>
  std::vector<glm::vec3> latticePoints(const int low, const int high, const Keep keep)
  {
    std::vector<glm::vec3> points;

    for (int x = low; x <= high; ++x)
    {
      for (int y = low; y <= high; ++y)
      {
        for (int z = low; z <= high; ++z)
        {
          if (keep(x, y, z))
          {
            points.emplace_back(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
          }
        }
      }
    }

    return points;
  }

  // Ordered by a random key per point, since the result of std::shuffle differs between standard libraries.
  std::vector<glm::vec3> shuffled(const std::vector<glm::vec3>& points, Lcg& random)
  {
    std::vector<std::pair<float, glm::vec3>> keyed;
    for (const auto& point : points)
    {
      keyed.emplace_back(random.next(), point);
    }

    std::ranges::stable_sort(keyed, [](const auto& left, const auto& right) { return left.first < right.first; });

    std::vector<glm::vec3> result;
    for (const auto& entry : keyed)
    {
      result.push_back(entry.second);
    }

    return result;
  }

  std::vector<glm::vec3> thinSlab(Lcg& random, const float thickness, const float offset)
  {
    std::vector<glm::vec3> points;

    for (int i = 0; i < 600; ++i)
    {
      const float x = random.next();
      const float y = random.next();
      const float z = random.next() * thickness;
      points.emplace_back(x + offset, y + offset, z + offset);
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

  expectSound(*hull, points, 1e-5);
}

TEST(ConvexHull, ATetrahedronIsFourVerticesAndFourOutwardTriangles)
{
  const std::vector<glm::vec3> points{ { 0, 0, 0 }, { 2, 0, 0 }, { 0, 2, 0 }, { 0, 0, 2 } };

  const auto hull = buildConvexHull(points);

  ASSERT_TRUE(hull.has_value());
  EXPECT_EQ(hull->vertices.size(), 4u);
  EXPECT_EQ(hull->indices.size(), 12u);
  expectSound(*hull, points, 1e-5);
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
  const auto points = randomCloud(500, glm::vec3(1.0f));

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
  const auto points = randomCloud(300, glm::vec3(3.0f, 1.0f, 0.5f));

  const auto hull = buildConvexHull(points);
  ASSERT_TRUE(hull.has_value());

  expectSound(*hull, points, 1e-4);
}

TEST(ConvexHull, ACloudWithMoreThanTheVertexLimitIsReducedToFit)
{
  const auto points = fibonacciSphere(2000);

  // Every point of a sphere is a hull vertex, so only the reduction can bring this under the limit.
  const auto hull = buildConvexHull(points);

  ASSERT_TRUE(hull.has_value());
  EXPECT_LE(hull->vertices.size(), maxConvexHullVertices);
  EXPECT_GT(hull->vertices.size(), 100u);
  expectSound(*hull, hull->vertices, 1e-4);

  // The reduced hull stays close around every input point.
  for (const auto& point : points)
  {
    EXPECT_LE(outsideDistance(*hull, point), 0.05);
  }

  // The positive control: a cloud that already fits is not reduced.
  const auto small = buildConvexHull(fibonacciSphere(200));
  ASSERT_TRUE(small.has_value());
  EXPECT_GT(small->vertices.size(), 150u);
}

TEST(ConvexHull, AHullJustOverTheLimitIsReducedFromItsOwnVertices)
{
  // 600 sphere points build a full hull first (over the limit but under quickHull's give-up size).
  const auto points = fibonacciSphere(600);
  const auto hull = buildConvexHull(points);

  ASSERT_TRUE(hull.has_value());
  EXPECT_LE(hull->vertices.size(), maxConvexHullVertices);
  EXPECT_GT(hull->vertices.size(), 100u);
  expectSound(*hull, hull->vertices, 1e-4);

  for (const auto& point : points)
  {
    EXPECT_LE(outsideDistance(*hull, point), 0.05);
  }
}

TEST(ConvexHull, ManyDegeneratePointsAreStillRefused)
{
  // The control: the same count of points spread over a volume builds.
  EXPECT_TRUE(buildConvexHull(randomCloud(2000, glm::vec3(1.0f))).has_value());

  const std::vector<glm::vec3> flat(2000, glm::vec3(1.0f, 2.0f, 3.0f));
  EXPECT_FALSE(buildConvexHull(flat).has_value());
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

TEST(ConvexHull, TheReducedHullUsesOnlyInputVertices)
{
  const auto points = fibonacciSphere(2000);
  const auto hull = buildConvexHull(points);

  ASSERT_TRUE(hull.has_value());
  ASSERT_LE(hull->vertices.size(), maxConvexHullVertices);

  for (const auto& vertex : hull->vertices)
  {
    EXPECT_NE(std::find(points.begin(), points.end(), vertex), points.end());
  }
}

TEST(ConvexHull, AThinSlabOfPointsNeverGivesAFoldedHull)
{
  // 1 x 1 x 0.001: the thickness is only a hundred times the build tolerance, where a closed but
  // non-convex hull is the failure to guard against.
  const auto points = randomCloud(600, glm::vec3(1.0f, 1.0f, 0.001f));

  const auto hull = buildConvexHull(points);
  ASSERT_TRUE(hull.has_value());
  expectSound(*hull, points, 1e-4);
}

TEST(ConvexHull, ANoisySubdividedCubeSurfaceNeverGivesAFoldedHull)
{
  // The grid points of a cube's surface, each jittered by about a build tolerance, so most of a face's
  // points sit within epsilon of its plane without lying on it.
  Lcg random;
  std::vector<glm::vec3> points;
  for (int x = 0; x < 6; ++x)
  {
    for (int y = 0; y < 6; ++y)
    {
      for (int z = 0; z < 6; ++z)
      {
        if (x != 0 && x != 5 && y != 0 && y != 5 && z != 0 && z != 5)
        {
          continue;
        }

        const float nx = random.next();
        const float ny = random.next();
        const float nz = random.next();
        points.emplace_back(static_cast<float>(x) / 5.0f + nx * 1e-5f, static_cast<float>(y) / 5.0f + ny * 1e-5f,
                            static_cast<float>(z) / 5.0f + nz * 1e-5f);
      }
    }
  }

  const auto hull = buildConvexHull(points);
  ASSERT_TRUE(hull.has_value());
  expectSound(*hull, points, 1e-4);
}

TEST(ConvexHull, ShuffledIntegerCubeSurfacesGiveAClosedHull)
{
  // Exactly collinear and coplanar lattice points: an apex in line with a horizon edge can leave the new
  // cone a zero-area face, and a hole in the hull.
  Lcg random;
  const auto lattice = latticePoints(0, 5, [](const int x, const int y, const int z) {
    return x % 5 == 0 || y % 5 == 0 || z % 5 == 0;
  });

  for (int attempt = 0; attempt < 60; ++attempt)
  {
    const auto points = shuffled(lattice, random);
    const auto hull = buildConvexHull(points);

    ASSERT_TRUE(hull.has_value()) << "attempt " << attempt;
    expectSound(*hull, points, 1e-4);
  }
}

TEST(ConvexHull, ShuffledIntegerBallsGiveAClosedHull)
{
  Lcg random;
  const auto lattice = latticePoints(-6, 6, [](const int x, const int y, const int z) {
    return x * x + y * y + z * z <= 36;
  });

  for (int attempt = 0; attempt < 8; ++attempt)
  {
    const auto points = shuffled(lattice, random);
    const auto hull = buildConvexHull(points);

    ASSERT_TRUE(hull.has_value()) << "attempt " << attempt;
    expectSound(*hull, points, 1e-4);
  }
}

TEST(ConvexHull, AThinDiscMissedPointsAreRepairedIntoASoundHull)
{
  // Two rings 2e-4 apart: points the first pass leaves outside an older face are handed back and re-expanded.
  Lcg random;
  std::vector<glm::vec3> points;
  for (int i = 0; i < 120; ++i)
  {
    const float angle = (random.next() + 1.0f) * std::numbers::pi_v<float>;
    const float side = random.next();
    const double angleD = static_cast<double>(angle);
    points.emplace_back(static_cast<float>(std::cos(angleD)), static_cast<float>(std::sin(angleD)),
                        side >= 0.0f ? 1e-4f : -1e-4f);
  }

  const auto hull = buildConvexHull(points);

  ASSERT_TRUE(hull.has_value());
  expectSound(*hull, points, 1e-4);
}

TEST(ConvexHull, ASlabBelowFloatResolutionIsRefusedAsDegenerateWhereTheSameSlabAtTheOriginBuilds)
{
  Lcg control;
  const auto atOrigin = thinSlab(control, 1e-4f, 0.0f);
  const auto hull = buildConvexHull(atOrigin);
  ASSERT_TRUE(hull.has_value());
  expectSound(*hull, atOrigin, 1e-4);

  // Floats near 10000 are about 1e-3 apart, so the thickness rounds away and the points are coplanar: the
  // initial simplex search refuses it as degenerate, before verification runs.
  Lcg distant;
  EXPECT_FALSE(buildConvexHull(thinSlab(distant, 1e-4f, 10000.0f)).has_value());
}
