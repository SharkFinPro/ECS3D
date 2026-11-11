#ifndef MANIFOLDINTERNAL_H
#define MANIFOLDINTERNAL_H

#include <glm/vec3.hpp>
#include <cstddef>
#include <span>
#include <vector>

// Helpers ContactManifold.cpp and FaceManifold.cpp share; not part of the narrow phase's interface.
namespace collisions::detail {
  inline constexpr float dedupeDistance = 1e-4f;

  inline constexpr float segmentEpsilon = 1e-12f;

  struct Candidate {
    glm::vec3 point;
    float depth;
  };

  [[nodiscard]] glm::vec3 centroidOf(std::span<const glm::vec3> points);

  [[nodiscard]] bool containsNear(const std::vector<glm::vec3>& points, const glm::vec3& point);

  [[nodiscard]] glm::vec3 closestPointOnSegment(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b);

  // How many float ulps of the coordinates two features may differ by and still be the same plane.
  inline constexpr float coincidentUlps = 64.0f;

  // One Sutherland-Hodgman pass against the half-space dot(inward, x - origin) >= -tolerance. Only a clear sign change
  // makes a new point.
  [[nodiscard]] std::vector<glm::vec3> clipAgainstPlane(const std::vector<glm::vec3>& input, const glm::vec3& origin,
                                                        const glm::vec3& inward, float tolerance);

  // The subject loop clipped by the side planes of the prism over polygon along normal (Sutherland-Hodgman). polygon
  // is counter-clockwise about normal; the subject keeps its position along normal.
  [[nodiscard]] std::vector<glm::vec3> clipToPolygon(std::vector<glm::vec3> subject,
                                                     const std::vector<glm::vec3>& polygon, const glm::vec3& normal);

  // The deepest candidate, then repeatedly the one furthest from everything chosen so far.
  [[nodiscard]] std::vector<glm::vec3> spreadOut(const std::vector<Candidate>& candidates, size_t maxPoints);
}

#endif //MANIFOLDINTERNAL_H
