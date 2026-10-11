#include "ConvexHull.h"
#include "QuickHull.h"
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numbers>

namespace {
  constexpr double coplanarDot = 1.0 - 1e-6;

  [[nodiscard]] bool allFinite(const std::span<const glm::vec3> points)
  {
    return std::ranges::all_of(points, [](const glm::vec3& point) {
      return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
    });
  }

  [[nodiscard]] glm::vec3 fibonacciDirection(const size_t index, const size_t count)
  {
    constexpr double goldenAngle = std::numbers::pi * (3.0 - 2.2360679774997896);

    const double y = 1.0 - 2.0 * (static_cast<double>(index) + 0.5) / static_cast<double>(count);
    const double radius = std::sqrt(std::max(0.0, 1.0 - y * y));
    const double theta = goldenAngle * static_cast<double>(index);

    return glm::vec3(static_cast<float>(std::cos(theta) * radius), static_cast<float>(y),
                     static_cast<float>(std::sin(theta) * radius));
  }

  [[nodiscard]] size_t supportIndex(const std::vector<glm::vec3>& vertices, const glm::vec3& direction)
  {
    size_t best = 0;
    float bestDot = glm::dot(vertices[0], direction);

    for (size_t v = 1; v < vertices.size(); ++v)
    {
      if (const float current = glm::dot(vertices[v], direction); current > bestDot)
      {
        bestDot = current;
        best = v;
      }
    }

    return best;
  }

  [[nodiscard]] std::vector<glm::vec3> reduceVertices(const std::vector<glm::vec3>& vertices)
  {
    std::vector<size_t> chosen;

    for (size_t i = 0; i < maxConvexHullVertices; ++i)
    {
      chosen.push_back(supportIndex(vertices, fibonacciDirection(i, maxConvexHullVertices)));
    }

    std::ranges::sort(chosen);
    chosen.erase(std::unique(chosen.begin(), chosen.end()), chosen.end());

    std::vector<glm::vec3> subset;
    subset.reserve(chosen.size());

    for (const size_t index : chosen)
    {
      subset.push_back(vertices[index]);
    }

    return subset;
  }

  [[nodiscard]] glm::dvec3 triangleNormal(const ConvexHull& hull, const size_t triangle)
  {
    const glm::dvec3 a(hull.vertices[hull.indices[triangle * 3]]);
    const glm::dvec3 b(hull.vertices[hull.indices[triangle * 3 + 1]]);
    const glm::dvec3 c(hull.vertices[hull.indices[triangle * 3 + 2]]);

    const glm::dvec3 cross = glm::cross(b - a, c - a);
    const double length = glm::length(cross);

    return length > 0.0 ? cross / length : glm::dvec3(0.0);
  }
}

std::optional<ConvexHull> buildConvexHull(const std::span<const glm::vec3> points)
{
  if (!allFinite(points))
  {
    return std::nullopt;
  }

  auto hull = quickHull(points);

  if (!hull || hull->vertices.size() <= maxConvexHullVertices)
  {
    return hull;
  }

  const auto subset = reduceVertices(hull->vertices);

  return quickHull(subset);
}

std::vector<HullEdge> hullEdges(const ConvexHull& hull)
{
  std::map<HullEdge, std::vector<size_t>> edgeTriangles;

  for (size_t triangle = 0; triangle < hull.indices.size() / 3; ++triangle)
  {
    for (size_t edge = 0; edge < 3; ++edge)
    {
      const uint32_t from = hull.indices[triangle * 3 + edge];
      const uint32_t to = hull.indices[triangle * 3 + (edge + 1) % 3];

      edgeTriangles[{ std::min(from, to), std::max(from, to) }].push_back(triangle);
    }
  }

  std::vector<HullEdge> edges;

  for (const auto& [edge, triangles] : edgeTriangles)
  {
    if (triangles.size() == 2
        && glm::dot(triangleNormal(hull, triangles[0]), triangleNormal(hull, triangles[1])) > coplanarDot)
    {
      continue;
    }

    edges.push_back(edge);
  }

  return edges;
}

bool isValidConvexHull(const ConvexHull& hull)
{
  if (hull.vertices.size() < 4 || hull.vertices.size() > maxConvexHullVertices
      || hull.indices.empty() || hull.indices.size() % 3 != 0 || hull.indices.size() > maxConvexHullIndices)
  {
    return false;
  }

  if (!allFinite(hull.vertices))
  {
    return false;
  }

  return std::ranges::all_of(hull.indices, [&hull](const uint32_t index) {
    return index < hull.vertices.size();
  });
}

ConvexHull makeUnitCubeHull()
{
  ConvexHull hull;

  for (uint32_t i = 0; i < 8; ++i)
  {
    hull.vertices.emplace_back((i & 1u) ? 1.0f : -1.0f, (i & 2u) ? 1.0f : -1.0f, (i & 4u) ? 1.0f : -1.0f);
  }

  hull.indices = {
    1, 3, 7,  1, 7, 5,
    0, 4, 6,  0, 6, 2,
    2, 7, 3,  2, 6, 7,
    0, 1, 5,  0, 5, 4,
    4, 5, 7,  4, 7, 6,
    0, 3, 1,  0, 2, 3
  };

  return hull;
}
