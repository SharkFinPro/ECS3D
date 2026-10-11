#ifndef QUICKHULL_H
#define QUICKHULL_H

#include "ConvexHull.h"
#include <glm/vec3.hpp>
#include <optional>
#include <span>

// The convex hull of the points with every point kept as a vertex candidate, however many survive: no
// vertex limit is applied. buildConvexHull wraps this with the input check and the reduction.
[[nodiscard]] std::optional<ConvexHull> quickHull(std::span<const glm::vec3> points);

#endif //QUICKHULL_H
