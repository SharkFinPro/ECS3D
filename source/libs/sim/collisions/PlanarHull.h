#ifndef PLANARHULL_H
#define PLANARHULL_H

#include <glm/vec3.hpp>
#include <span>
#include <vector>

namespace collisions {
  // The points that are corners of the convex hull of points seen along normal, counter-clockwise about it.
  // A point on a hull edge is dropped, and fewer than three points come back as they are.
  [[nodiscard]] std::vector<glm::vec3> convexOutline(std::span<const glm::vec3> points, const glm::vec3& normal);
}

#endif //PLANARHULL_H
