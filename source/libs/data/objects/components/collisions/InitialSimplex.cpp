#include "InitialSimplex.h"
#include <glm/geometric.hpp>
#include <cmath>
#include <cstddef>

namespace hull {
namespace {
  [[nodiscard]] std::array<uint32_t, 6> findExtremes(const std::vector<glm::dvec3>& points)
  {
    std::array<uint32_t, 6> extremes{};

    for (uint32_t i = 1; i < points.size(); ++i)
    {
      for (int axis = 0; axis < 3; ++axis)
      {
        if (points[i][axis] < points[extremes[axis * 2]][axis])
        {
          extremes[axis * 2] = i;
        }

        if (points[i][axis] > points[extremes[axis * 2 + 1]][axis])
        {
          extremes[axis * 2 + 1] = i;
        }
      }
    }

    return extremes;
  }

  [[nodiscard]] bool findThirdPoint(const std::vector<glm::dvec3>& points, const double eps,
                                    std::array<uint32_t, 4>& simplex)
  {
    const glm::dvec3 origin = points[simplex[0]];
    const glm::dvec3 direction = glm::normalize(points[simplex[1]] - origin);

    double bestDistance = -1.0;
    for (uint32_t i = 0; i < points.size(); ++i)
    {
      const double distance = glm::length(glm::cross(points[i] - origin, direction));

      if (distance > bestDistance)
      {
        bestDistance = distance;
        simplex[2] = i;
      }
    }

    return bestDistance > eps;
  }

  [[nodiscard]] bool findFourthPoint(const std::vector<glm::dvec3>& points, const double eps,
                                     std::array<uint32_t, 4>& simplex)
  {
    const glm::dvec3 origin = points[simplex[0]];
    const glm::dvec3 normal = glm::normalize(
      glm::cross(points[simplex[1]] - origin, points[simplex[2]] - origin));

    double bestDistance = -1.0;
    for (uint32_t i = 0; i < points.size(); ++i)
    {
      const double distance = std::abs(glm::dot(normal, points[i] - origin));

      if (distance > bestDistance)
      {
        bestDistance = distance;
        simplex[3] = i;
      }
    }

    return bestDistance > eps;
  }
}

bool findInitialSimplex(const std::vector<glm::dvec3>& points, const double eps, std::array<uint32_t, 4>& simplex)
{
  const auto extremes = findExtremes(points);

  double bestDistance = -1.0;
  for (size_t i = 0; i < extremes.size(); ++i)
  {
    for (size_t j = i + 1; j < extremes.size(); ++j)
    {
      const double distance = glm::distance(points[extremes[i]], points[extremes[j]]);

      if (distance > bestDistance)
      {
        bestDistance = distance;
        simplex[0] = extremes[i];
        simplex[1] = extremes[j];
      }
    }
  }

  if (bestDistance <= eps)
  {
    return false;
  }

  return findThirdPoint(points, eps, simplex) && findFourthPoint(points, eps, simplex);
}
}
