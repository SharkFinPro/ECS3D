#ifndef HULLQUERIES_H
#define HULLQUERIES_H

#include <glm/vec3.hpp>
#include <cstdint>
#include <span>

// Analytic queries against a convex hull given as world-space vertices and counter-clockwise (seen from
// outside) triangle indices. Used by SceneQueries for ConvexMeshCollider.
namespace hullQueries {
  // Ray vs hull by clipping the ray against every face plane. dir must be normalized. A ray starting inside
  // reports contact at distance 0. Writes tHit and the entered face's outward normal only on a hit within
  // maxDistance.
  [[nodiscard]] bool rayHull(const glm::vec3& origin, const glm::vec3& dir, std::span<const glm::vec3> vertices,
                             std::span<const uint32_t> indices, float maxDistance, float& tHit, glm::vec3& normal);

  // Whether the sphere touches the hull: its center is inside every face plane, or the nearest point of the
  // hull's surface is within the radius.
  [[nodiscard]] bool sphereOverlapsHull(const glm::vec3& center, float radius, std::span<const glm::vec3> vertices,
                                        std::span<const uint32_t> indices);

  // The nearest point to p on triangle abc.
  [[nodiscard]] glm::vec3 closestPointOnTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b,
                                                 const glm::vec3& c);
}

#endif //HULLQUERIES_H
