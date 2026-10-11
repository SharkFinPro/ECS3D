#ifndef CAPSULEGIZMO_H
#define CAPSULEGIZMO_H

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <array>

// A capsule outline is drawn as a unit cylinder (radius 1, y in [-1, 1]) plus two unit spheres, all in the
// one outline color so their silhouettes merge. This is where each part goes for a capsule whose core segment
// runs from start to end; the cylinder takes the capsule's own orientation.
struct CapsuleGizmoLayout {
  glm::vec3 cylinderPosition;
  glm::vec3 cylinderScale;
  bool hasCylinder;
  std::array<glm::vec3, 2> capPositions;
  glm::vec3 capScale;
};

// Below this the core segment is a point, and the two spheres already make up the whole capsule.
inline constexpr float capsuleGizmoMinHalfLength = 1e-4f;

[[nodiscard]] inline CapsuleGizmoLayout layoutCapsuleGizmo(const glm::vec3& start, const glm::vec3& end,
                                                           const float radius)
{
  const float halfLength = glm::length(end - start) * 0.5f;

  return {
    .cylinderPosition = (start + end) * 0.5f,
    .cylinderScale = glm::vec3(radius, halfLength, radius),
    .hasCylinder = halfLength > capsuleGizmoMinHalfLength,
    .capPositions = { start, end },
    .capScale = glm::vec3(radius)
  };
}

#endif //CAPSULEGIZMO_H
