#ifndef FACEMANIFOLD_H
#define FACEMANIFOLD_H

#include <glm/vec3.hpp>
#include <cstddef>
#include <cstdint>
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

  // The contact points of two convex shapes from the face of each that most nearly faces the other, the way a
  // box pair is built. normal points from first toward second. The side whose face is more nearly parallel to
  // the normal is the reference, ties going to first. The other side's touching vertices are clamped into the
  // reference face's polygon along the face plane, keeping their depth along it. At most maxPoints, deepest
  // first and then spread out. Empty when either shape has no faces.
  [[nodiscard]] std::vector<glm::vec3> referenceFaceManifold(const FaceShape& first, const FaceShape& second,
                                                             const glm::vec3& normal, size_t maxPoints);
}

#endif //FACEMANIFOLD_H
