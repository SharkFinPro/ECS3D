#ifndef SUPPORTFACES_H
#define SUPPORTFACES_H

#include <objects/components/collisions/WorldFace.h>
#include <glm/vec3.hpp>
#include <optional>
#include <span>
#include <vector>

class Collider;
class ConvexMeshCollider;

namespace collisions {
  using SupportFace = WorldFace;

  // The planar faces a body can rest on: the six faces of a box, and the faces of a convex mesh with
  // coplanar triangles merged into one polygon. A sphere or capsule has none, and a triangle with no area
  // is ignored.
  [[nodiscard]] std::vector<SupportFace> supportFacesOf(Collider& collider);

  // A convex mesh's faces, as supportFacesOf returns them, kept on the collider until its world vertices change.
  [[nodiscard]] const std::vector<SupportFace>& hullSupportFaces(ConvexMeshCollider& collider);

  // The one face of the collider that supportFacesOf would return that points most along direction. A box builds
  // only that face; a collider with no faces gives nullopt.
  [[nodiscard]] std::optional<SupportFace> supportFaceToward(Collider& collider, const glm::vec3& direction);

  // The face whose normal points most along direction, or nullptr when there are no faces. Among equally
  // aligned faces, the first wins.
  [[nodiscard]] const SupportFace* faceToward(std::span<const SupportFace> faces, const glm::vec3& direction);

  // Pulls a point back over the face's footprint, leaving its height along the face normal alone.
  [[nodiscard]] glm::vec3 clampToFace(const SupportFace& face, const glm::vec3& point);
}

#endif //SUPPORTFACES_H
