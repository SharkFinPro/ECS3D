#ifndef HULLVERIFICATION_H
#define HULLVERIFICATION_H

#include "HullFace.h"
#include <glm/vec3.hpp>
#include <cstddef>
#include <vector>

namespace hull {
// Reassigning orphans only to the new faces can lose a point that lies outside an older face as well. This hands
// each such point to the first live face it lies outside of, and returns the live faces, or nothing when no
// point was missed. Hull vertices are never handed back.
[[nodiscard]] std::vector<size_t> assignMissedPoints(std::vector<HullFace>& faces,
                                                     const std::vector<glm::dvec3>& points, double eps);

// The hull is a closed surface with no zero-area face, and every input point, vertices included, lies inside
// every face plane to within ten epsilons: it contains the input and is convex. aliveFaces is the count of
// alive faces, normal or not.
[[nodiscard]] bool isVerifiedHull(const std::vector<HullFace>& faces, size_t aliveFaces,
                                  const std::vector<glm::dvec3>& points, double eps);
}

#endif //HULLVERIFICATION_H
