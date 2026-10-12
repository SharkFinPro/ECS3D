#ifndef INITIALSIMPLEX_H
#define INITIALSIMPLEX_H

#include <glm/vec3.hpp>
#include <array>
#include <cstdint>
#include <vector>

namespace hull {
  // Picks four points that span a tetrahedron with every edge, and the height over its base, above eps.
  // False when the points are (nearly) a point, a line or a plane.
  [[nodiscard]] bool findInitialSimplex(const std::vector<glm::dvec3>& points, double eps,
                                        std::array<uint32_t, 4>& simplex);
}

#endif //INITIALSIMPLEX_H
