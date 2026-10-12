#include "ConvexHull.h"
#include "QuickHull.h"
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>

namespace {
  [[nodiscard]] bool allFinite(const std::span<const glm::vec3> points)
  {
    return std::ranges::all_of(points, [](const glm::vec3& point) {
      return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
    });
  }
}

std::optional<ConvexHull> buildConvexHull(const std::span<const glm::vec3> points)
{
  if (!allFinite(points))
  {
    return std::nullopt;
  }

  auto hull = quickHull(points);

  if (hull && hull->vertices.size() > maxConvexHullVertices)
  {
    return std::nullopt;
  }

  return hull;
}
