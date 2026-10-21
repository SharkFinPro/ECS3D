#include "RestPose.h"
#include "objects/components/RotationConvention.h"
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace {
  struct HullFacet
  {
    glm::vec3 normal;
    float area;
    std::vector<std::pair<uint32_t, uint32_t>> edges;
  };

  // The hull's flat faces: coplanar triangles merged, areas summed, and the edges that bound the merged face.
  std::vector<HullFacet> facetsOf(const ConvexHull& hull)
  {
    std::vector<HullFacet> facets;

    for (size_t i = 0; i + 2 < hull.indices.size(); i += 3)
    {
      const auto& a = hull.vertices[hull.indices[i]];
      const auto cross = glm::cross(hull.vertices[hull.indices[i + 1]] - a, hull.vertices[hull.indices[i + 2]] - a);
      const float length = glm::length(cross);
      if (length < 1e-9f)
      {
        continue;
      }

      const auto normal = cross / length;
      auto match = std::find_if(facets.begin(), facets.end(), [&normal](const HullFacet& facet) {
        return glm::dot(facet.normal, normal) > 0.9999f;
      });

      if (match == facets.end())
      {
        facets.push_back({ normal, 0.0f, {} });
        match = facets.end() - 1;
      }

      match->area += length / 2.0f;
      for (size_t k = 0; k < 3; ++k)
      {
        const auto from = hull.indices[i + k];
        const auto to = hull.indices[i + (k + 1) % 3];
        const auto shared = std::find(match->edges.begin(), match->edges.end(), std::make_pair(to, from));

        if (shared != match->edges.end())
        {
          match->edges.erase(shared);
        }
        else
        {
          match->edges.emplace_back(from, to);
        }
      }
    }

    return facets;
  }

  // The hull's volume centroid, from tetrahedra to its triangles.
  glm::vec3 volumeCentroid(const ConvexHull& hull)
  {
    glm::vec3 weighted(0.0f);
    float volume = 0.0f;

    for (size_t i = 0; i + 2 < hull.indices.size(); i += 3)
    {
      const auto& a = hull.vertices[hull.indices[i]];
      const auto& b = hull.vertices[hull.indices[i + 1]];
      const auto& c = hull.vertices[hull.indices[i + 2]];
      const float tetrahedron = glm::dot(a, glm::cross(b, c)) / 6.0f;

      weighted += tetrahedron * (a + b + c) / 4.0f;
      volume += tetrahedron;
    }

    return weighted / volume;
  }

  // How far inside the facet's outline the centroid lands when dropped onto its plane; negative when outside.
  float containmentMargin(const ConvexHull& hull, const HullFacet& facet, const glm::vec3& centroid)
  {
    float margin = std::numeric_limits<float>::max();

    for (const auto& [from, to] : facet.edges)
    {
      const auto& p = hull.vertices[from];
      const auto inward = glm::normalize(glm::cross(facet.normal, hull.vertices[to] - p));
      margin = std::min(margin, glm::dot(centroid - p, inward));
    }

    return margin;
  }

  // The largest face the center of mass sits well inside (at unit scale), else the best-contained one.
  const HullFacet& stablestFacet(const ConvexHull& hull, const std::vector<HullFacet>& facets)
  {
    constexpr float requiredMargin = 0.05f;
    const auto centroid = volumeCentroid(hull);

    const HullFacet* best = &facets.front();
    float bestMargin = containmentMargin(hull, *best, centroid);
    for (const auto& facet : facets)
    {
      const float margin = containmentMargin(hull, facet, centroid);
      const bool bothStable = margin >= requiredMargin && bestMargin >= requiredMargin;
      const bool better = bothStable ? facet.area > best->area : margin > bestMargin;

      if (better)
      {
        best = &facet;
        bestMargin = margin;
      }
    }

    return *best;
  }
}

namespace defaultproject {

RestPose restPose(const ConvexHull& hull)
{
  const auto facets = facetsOf(hull);
  const auto& largest = stablestFacet(hull, facets);

  const glm::vec3 down(0.0f, -1.0f, 0.0f);
  const auto axis = glm::cross(largest.normal, down);
  const float angle = std::acos(std::clamp(glm::dot(largest.normal, down), -1.0f, 1.0f));
  const auto turn = glm::length(axis) < 1e-6f ? glm::angleAxis(angle, glm::vec3(1.0f, 0.0f, 0.0f))
                                              : glm::angleAxis(angle, glm::normalize(axis));

  const auto rotation = quatToEulerDegrees(turn);
  const auto orientation = eulerDegreesToQuat(rotation);
  float lowest = (orientation * hull.vertices.front()).y;
  for (const auto& vertex : hull.vertices)
  {
    lowest = std::min(lowest, (orientation * vertex).y);
  }

  return { rotation, lowest };
}

}
