#ifndef CONTACTMANIFOLD_H
#define CONTACTMANIFOLD_H

#include <glm/vec3.hpp>
#include <cstddef>
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
