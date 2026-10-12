#include "ConvexHull.h"
#include "QuickHull.h"
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace {
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

  // Strict '>' keeps the lowest index on ties, and the sorted unique indices keep the subset's order fixed.
  [[nodiscard]] std::vector<glm::vec3> reduceVertices(const std::vector<glm::vec3>& vertices)
  {
    std::vector<size_t> chosen;
    chosen.reserve(maxConvexHullVertices);

    for (size_t i = 0; i < maxConvexHullVertices; ++i)
    {
      chosen.push_back(supportIndex(vertices, fibonacciDirection(i, maxConvexHullVertices)));
    }

    std::sort(chosen.begin(), chosen.end());
    chosen.erase(std::unique(chosen.begin(), chosen.end()), chosen.end());

    std::vector<glm::vec3> subset;
    subset.reserve(chosen.size());

    for (const size_t index : chosen)
    {
      subset.push_back(vertices[index]);
    }

    return subset;
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
