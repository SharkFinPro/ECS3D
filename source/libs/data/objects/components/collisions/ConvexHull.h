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

// A hull of V vertices has at most 2V - 4 triangles.
constexpr size_t maxConvexHullIndices = 3 * (2 * maxConvexHullVertices - 4);

using HullEdge = std::pair<uint32_t, uint32_t>;

// 3D Quickhull. Returns nullopt for non-finite input, fewer than four distinct points, or points that are
// (nearly) coplanar. Interior and duplicate points are dropped. A hull over maxConvexHullVertices is
// rebuilt from a deterministic subset of its vertices.
[[nodiscard]] std::optional<ConvexHull> buildConvexHull(std::span<const glm::vec3> points);

// The hull's unique edges as ascending index pairs, in ascending order. An edge between two coplanar
// triangles is skipped, so the diagonal of a flat quad is not an edge.
[[nodiscard]] std::vector<HullEdge> hullEdges(const ConvexHull& hull);

// True when the hull has 4..maxConvexHullVertices finite vertices and a non-empty triangle list whose
// indices all name a vertex.
[[nodiscard]] bool isValidConvexHull(const ConvexHull& hull);

// The axis-aligned cube with corners at +/-1.
[[nodiscard]] ConvexHull makeUnitCubeHull();

#endif //CONVEXHULL_H
