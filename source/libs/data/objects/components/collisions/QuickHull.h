#ifndef QUICKHULL_H
#define QUICKHULL_H

#include "ConvexHull.h"
#include <glm/vec3.hpp>
#include <optional>
#include <span>

// The convex hull of the points. Gives up early once the hull in progress is far past maxConvexHullVertices;
// buildConvexHull wraps this with the input check and the exact vertex limit.
[[nodiscard]] std::optional<ConvexHull> quickHull(std::span<const glm::vec3> points);

#endif //QUICKHULL_H
