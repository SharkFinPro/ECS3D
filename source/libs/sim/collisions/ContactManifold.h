#ifndef CONTACTMANIFOLD_H
#define CONTACTMANIFOLD_H

#include <glm/vec3.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace collisions {
  // The vertices of a shape that touch along direction: those within a tolerance, scaled to the shape's
  // size, of the extreme one. One vertex for a corner, two for an edge, three or more for a face.
  [[nodiscard]] std::vector<glm::vec3> touchingFeature(std::span<const glm::vec3> vertices,
                                                       const glm::vec3& direction);

  // Same, with the tolerance given: the vertices within epsilon of the extreme one along direction.
  [[nodiscard]] std::vector<glm::vec3> touchingFeature(std::span<const glm::vec3> vertices,
                                                       const glm::vec3& direction, float epsilon);

  // A convex shape as plain geometry: its world-space vertices, the unit outward normals of its faces (several
  // may repeat one plane) and the tolerance within which a vertex touches along a direction.
  struct FaceShape {
    std::span<const glm::vec3> vertices;
    std::span<const glm::vec3> faceNormals;
    float featureEpsilon;
  };

  // The unit outward normals of a triangle mesh's faces. Degenerate triangles are skipped.
  [[nodiscard]] std::vector<glm::vec3> hullFaceNormals(std::span<const glm::vec3> vertices,
                                                       std::span<const uint32_t> indices);

  // The contact points of two convex shapes from the face of each that most nearly faces the other, the way a
  // box pair is built. normal points from first toward second. The side whose face is more nearly parallel to
  // the normal is the reference, ties going to first. The other side's touching vertices are clamped into the
  // reference face's polygon along the face plane, keeping their depth along it. At most maxPoints, deepest
  // first and then spread out. Empty when either shape has no faces.
  [[nodiscard]] std::vector<glm::vec3> referenceFaceManifold(const FaceShape& first, const FaceShape& second,
                                                             const glm::vec3& normal, size_t maxPoints);

  // The contact points of two convex shapes by clipping, in world space and at most maxPoints of them.
  //
  // normal is the unit contact normal pointing from the first shape toward the second. featureA is the
  // first shape's touchingFeature along normal and featureB the second's along -normal. The side with the
  // more points is the reference; the other side's feature is clipped to the reference polygon's edge
  // planes and projected onto its plane. Features with no polygon between them reduce to the one point
  // midway between their closest points. Never empty for non-empty features.
  [[nodiscard]] std::vector<glm::vec3> clipManifold(std::span<const glm::vec3> featureA,
                                                    std::span<const glm::vec3> featureB,
                                                    const glm::vec3& normal, size_t maxPoints);
}

#endif //CONTACTMANIFOLD_H
