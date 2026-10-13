#ifndef HULLGIZMOMESH_H
#define HULLGIZMOMESH_H

#include <objects/components/collisions/ConvexHull.h>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

// Triangle soup for drawing a hull: three vertices per triangle, each carrying the triangle's flat normal, so
// the faces shade (and outline) crisply. Positions stay in the hull's local space.
struct HullGizmoMesh {
  std::vector<glm::vec3> positions;
  std::vector<glm::vec3> normals;
  std::vector<uint32_t> indices;
};

// Skips a triangle with an index past the vertices or with no area (a hull from the wire may carry one),
// since it has no normal; an empty hull gives an empty mesh.
[[nodiscard]] inline HullGizmoMesh buildHullGizmoMesh(const ConvexHull& hull)
{
  HullGizmoMesh mesh;
  const size_t vertexCount = hull.vertices.size();

  for (size_t i = 0; i + 2 < hull.indices.size(); i += 3)
  {
    const uint32_t ia = hull.indices[i];
    const uint32_t ib = hull.indices[i + 1];
    const uint32_t ic = hull.indices[i + 2];

    if (ia >= vertexCount || ib >= vertexCount || ic >= vertexCount)
    {
      continue;
    }

    const glm::vec3& a = hull.vertices[ia];
    const glm::vec3& b = hull.vertices[ib];
    const glm::vec3& c = hull.vertices[ic];

    const glm::vec3 cross = glm::cross(b - a, c - a);
    const float length = glm::length(cross);

    if (!(length > 0.0f))
    {
      continue;
    }

    const glm::vec3 normal = cross / length;
    const auto first = static_cast<uint32_t>(mesh.positions.size());

    for (const glm::vec3* corner : { &a, &b, &c })
    {
      mesh.positions.push_back(*corner);
      mesh.normals.push_back(normal);
    }

    mesh.indices.push_back(first);
    mesh.indices.push_back(first + 1);
    mesh.indices.push_back(first + 2);
  }

  return mesh;
}

#endif //HULLGIZMOMESH_H
