#ifndef CONVEXHULL_H
#define CONVEXHULL_H

#include <glm/vec3.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

// Triangles are counter-clockwise when seen from outside the hull.
struct ConvexHull {
  std::vector<glm::vec3> vertices;
  std::vector<uint32_t> indices;
};

constexpr size_t maxConvexHullVertices = 256;

using HullEdge = std::pair<uint32_t, uint32_t>;

// 3D Quickhull. Returns nullopt for non-finite input, fewer than four distinct points, or points that are
// (nearly) coplanar. Interior and duplicate points are dropped. A hull over maxConvexHullVertices is refused, and
// so is any input the builder cannot verify as a closed convex hull around every point.
[[nodiscard]] std::optional<ConvexHull> buildConvexHull(std::span<const glm::vec3> points);

#endif //CONVEXHULL_H
