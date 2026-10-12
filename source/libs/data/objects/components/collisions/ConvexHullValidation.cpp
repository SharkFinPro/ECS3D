#include "ConvexHullValidation.h"
#include "../FiniteCheck.h"
#include <algorithm>

bool isValidConvexHull(const ConvexHull& hull)
{
  if (hull.vertices.size() < 4 || hull.vertices.size() > maxConvexHullVertices
      || hull.indices.empty() || hull.indices.size() % 3 != 0 || hull.indices.size() > maxConvexHullIndices)
  {
    return false;
  }

  const bool allFinite = std::ranges::all_of(hull.vertices, [](const glm::vec3& vertex) {
    return finiteCheck::isFinite(vertex);
  });

  if (!allFinite)
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
