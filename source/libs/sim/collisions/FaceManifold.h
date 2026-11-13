#ifndef FACEMANIFOLD_H
#define FACEMANIFOLD_H

#include <glm/vec3.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace collisions {
  // Faces whose alignment with the contact normal differs by less than this count as equally flat, so float
  // noise cannot swap which side is the reference. Box pairs use the same band.
  inline constexpr float faceAlignmentTie = 2e-6f;

  // A convex shape as plain geometry: its world-space vertices, the unit outward normals of its faces (several
  // may repeat one plane) and the tolerance within which a vertex touches along a direction.
  struct FaceShape {
    std::span<const glm::vec3> vertices;
    std::span<const glm::vec3> faceNormals;
    float featureEpsilon;
  };

  // The unit outward normals of a triangle mesh's faces. Degenerate and nearly collinear triangles are skipped.
  [[nodiscard]] std::vector<glm::vec3> hullFaceNormals(std::span<const glm::vec3> vertices,
                                                       std::span<const uint32_t> indices);

  // The contact points of two convex shapes from the face of each that most nearly faces the other, the way a box
  // pair is built. normal points from first toward second. The side whose face is more nearly parallel to the normal
  // is the reference, ties going to first. The other side's touching face, or an edge of it, is clipped to the
  // reference face's side planes and cut at its plane, so the points are the overlap polygon of the two faces and
  // stay on the other side's feature; a corner, or an edge or face that clips to nothing, is clamped into the
  // reference polygon instead. At most maxPoints: with four, the deepest corner and the three that cover the most
  // area, otherwise spread by depth and distance. Empty when either shape has no faces.
  [[nodiscard]] std::vector<glm::vec3> referenceFaceManifold(const FaceShape& first, const FaceShape& second,
                                                             const glm::vec3& normal, size_t maxPoints);

  // The reference face's normal, turned to point from first toward second, when it lies within a degree or so of
  // normal: the two shapes then meet face on, and the face says which way more exactly than EPA's polytope does.
  [[nodiscard]] std::optional<glm::vec3> referenceFaceNormal(const FaceShape& first, const FaceShape& second,
                                                             const glm::vec3& normal);

  // How far short of the reference face a point of the other side's face may be and still be reported. A face
  // about to land flat then reports the corners that have not arrived yet, and the solver can stop them where
  // they arrive instead of meeting them a tick later, already past the face and turning.
  inline constexpr float contactMargin = 0.1f;

  struct ManifoldPoint {
    glm::vec3 point;
    // How far the point is past the reference face; negative for a point still short of it.
    float depth;
  };

  // referenceFaceManifold with each point's depth. The other side's vertices within margin of its touching one
  // count as its feature, and the overlap is kept up to margin short of the reference face.
  [[nodiscard]] std::vector<ManifoldPoint> referenceFaceContacts(const FaceShape& first, const FaceShape& second,
                                                                 const glm::vec3& normal, size_t maxPoints,
                                                                 float margin);
}

#endif //FACEMANIFOLD_H
