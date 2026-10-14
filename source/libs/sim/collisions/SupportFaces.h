#ifndef SUPPORTFACES_H
#define SUPPORTFACES_H

#include <glm/vec3.hpp>
#include <span>
#include <vector>

class Collider;

namespace collisions {
  // One planar face of a convex shape in world space. The corners run counter-clockwise seen from outside,
  // so the polygon is convex and its winding agrees with the outward unit normal.
  struct SupportFace {
    glm::vec3 normal{ 0.0f };
    glm::vec3 center{ 0.0f };
    std::vector<glm::vec3> corners;
  };

  // The planar faces a body can rest on: the six faces of a box, and the faces of a convex mesh with
  // coplanar triangles merged into one polygon. A sphere or capsule has none, and a triangle with no area
  // is ignored.
  [[nodiscard]] std::vector<SupportFace> supportFacesOf(Collider& collider);

  // The face whose normal points most along direction, or nullptr when there are no faces. Among equally
  // aligned faces, the first wins.
  [[nodiscard]] const SupportFace* faceToward(std::span<const SupportFace> faces, const glm::vec3& direction);

  // Pulls a point back over the face's footprint, leaving its height along the face normal alone.
  [[nodiscard]] glm::vec3 clampToFace(const SupportFace& face, const glm::vec3& point);
}

#endif //SUPPORTFACES_H
