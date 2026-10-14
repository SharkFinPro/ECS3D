#include "SupportFaces.h"
#include <objects/components/collisions/Collider.h>
#include <objects/components/collisions/ConvexMeshCollider.h>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <utility>

namespace {
  using collisions::SupportFace;

  // Triangles whose normals agree to about a quarter of a degree and whose planes lie within this share of
  // the hull's size are one face.
  constexpr float coplanarCosine = 1.0f - 1e-5f;
  constexpr float coplanarDistanceShare = 1e-4f;

  // A triangle whose doubled area is below this share of its longest edge squared has no direction.
  constexpr float degenerateAreaShare = 1e-6f;

  // A corner whose edges' cross product is below this share of the product of their lengths lies on a
  // straight edge.
  constexpr float collinearShare = 1e-5f;

  std::vector<SupportFace> boxFacesOf(Collider& collider)
  {
    const auto center = collider.getPosition();
    const auto axes = glm::mat3_cast(glm::quat(glm::radians(collider.getRotation())));
    const auto halfExtents = glm::abs(collider.getScale());

    std::vector<SupportFace> faces;
    for (int axis = 0; axis < 3; ++axis)
    {
      const int u = (axis + 1) % 3;
      const int v = (axis + 2) % 3;
      const auto alongU = axes[u] * halfExtents[u];
      const auto alongV = axes[v] * halfExtents[v];

      for (const float sign : { 1.0f, -1.0f })
      {
        SupportFace face;
        face.normal = axes[axis] * sign;
        face.center = center + face.normal * halfExtents[axis];
        face.corners = {
          face.center - alongU - alongV,
          face.center + alongU - alongV,
          face.center + alongU + alongV,
          face.center - alongU + alongV
        };

        if (sign < 0.0f)
        {
          std::ranges::reverse(face.corners);
        }

        faces.push_back(std::move(face));
      }
    }

    return faces;
  }

  struct Triangle {
    std::array<uint32_t, 3> v{};
    glm::vec3 cross{ 0.0f };
    glm::vec3 normal{ 0.0f };
  };

  glm::vec3 centroidOf(const std::vector<glm::vec3>& vertices)
  {
    glm::vec3 centroid{ 0.0f };
    for (const auto& vertex : vertices)
    {
      centroid += vertex;
    }

    return centroid / static_cast<float>(vertices.size());
  }

  // Triangles wound outward from the hull's centroid, whatever the transform did to the winding, and
  // without those that have no area.
  std::vector<Triangle> outwardTriangles(const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& indices)
  {
    const auto centroid = centroidOf(vertices);

    std::vector<Triangle> triangles;
    for (size_t i = 0; i + 2 < indices.size(); i += 3)
    {
      Triangle triangle{ { indices[i], indices[i + 1], indices[i + 2] } };
      const auto& a = vertices[triangle.v[0]];
      const auto& b = vertices[triangle.v[1]];
      const auto& c = vertices[triangle.v[2]];

      triangle.cross = glm::cross(b - a, c - a);
      const float longest = std::max({ glm::dot(b - a, b - a), glm::dot(c - b, c - b), glm::dot(a - c, a - c) });
      const float doubledArea = glm::length(triangle.cross);
      if (!(doubledArea > degenerateAreaShare * longest))
      {
        continue;
      }

      if (glm::dot(triangle.cross, (a + b + c) / 3.0f - centroid) < 0.0f)
      {
        std::swap(triangle.v[1], triangle.v[2]);
        triangle.cross = -triangle.cross;
      }

      triangle.normal = triangle.cross / doubledArea;
      triangles.push_back(triangle);
    }

    return triangles;
  }

  float extentOf(const std::vector<glm::vec3>& vertices)
  {
    glm::vec3 low(std::numeric_limits<float>::max());
    glm::vec3 high(std::numeric_limits<float>::lowest());
    for (const auto& vertex : vertices)
    {
      low = glm::min(low, vertex);
      high = glm::max(high, vertex);
    }

    return glm::length(high - low);
  }

  // Each triangle joins the first group whose seed it is coplanar with.
  std::vector<std::vector<Triangle>> groupCoplanar(const std::vector<Triangle>& triangles,
                                                   const std::vector<glm::vec3>& vertices, const float tolerance)
  {
    std::vector<std::vector<Triangle>> groups;
    for (const auto& triangle : triangles)
    {
      const auto joins = [&](const std::vector<Triangle>& group)
      {
        const auto& seed = group.front();

        return glm::dot(seed.normal, triangle.normal) >= coplanarCosine &&
               std::fabs(glm::dot(seed.normal, vertices[triangle.v[0]] - vertices[seed.v[0]])) <= tolerance;
      };

      if (const auto group = std::ranges::find_if(groups, joins); group != groups.end())
      {
        group->push_back(triangle);
      }
      else
      {
        groups.push_back({ triangle });
      }
    }

    return groups;
  }

  // The directed edges of a group that no triangle in it traverses the other way, chained into one loop.
  // Empty when the boundary is not a single simple loop.
  std::vector<uint32_t> boundaryLoop(const std::vector<Triangle>& group)
  {
    std::map<std::pair<uint32_t, uint32_t>, int> edges;
    for (const auto& triangle : group)
    {
      for (int i = 0; i < 3; ++i)
      {
        ++edges[{ triangle.v[i], triangle.v[(i + 1) % 3] }];
      }
    }

    std::map<uint32_t, uint32_t> next;
    for (const auto& [edge, count] : edges)
    {
      if (count != 1 || edges.contains({ edge.second, edge.first }))
      {
        continue;
      }

      if (!next.try_emplace(edge.first, edge.second).second)
      {
        return {};
      }
    }

    if (next.size() < 3)
    {
      return {};
    }

    const uint32_t start = next.begin()->first;
    std::vector<uint32_t> loop{ start };
    for (auto at = next.find(next.begin()->second); at != next.end() && at->first != start; at = next.find(at->second))
    {
      loop.push_back(at->first);
      if (loop.size() > next.size())
      {
        return {};
      }
    }

    return loop.size() == next.size() ? loop : std::vector<uint32_t>{};
  }

  std::vector<glm::vec3> withoutStraightCorners(const std::vector<glm::vec3>& corners)
  {
    std::vector<glm::vec3> kept;
    for (size_t i = 0; i < corners.size(); ++i)
    {
      const auto& before = corners[(i + corners.size() - 1) % corners.size()];
      const auto& after = corners[(i + 1) % corners.size()];
      const auto in = corners[i] - before;
      const auto out = after - corners[i];

      if (glm::length(glm::cross(in, out)) > collinearShare * glm::length(in) * glm::length(out))
      {
        kept.push_back(corners[i]);
      }
    }

    return kept;
  }

  glm::vec3 areaCentroid(const std::vector<glm::vec3>& corners, const glm::vec3& normal)
  {
    glm::vec3 weighted{ 0.0f };
    float total = 0.0f;
    for (size_t i = 1; i + 1 < corners.size(); ++i)
    {
      const float area = glm::dot(glm::cross(corners[i] - corners[0], corners[i + 1] - corners[0]), normal);
      weighted += area * (corners[0] + corners[i] + corners[i + 1]) / 3.0f;
      total += area;
    }

    return weighted / total;
  }

  std::optional<SupportFace> faceOfGroup(const std::vector<Triangle>& group, const std::vector<glm::vec3>& vertices)
  {
    glm::vec3 summed{ 0.0f };
    for (const auto& triangle : group)
    {
      summed += triangle.cross;
    }

    std::vector<glm::vec3> loop;
    for (const auto index : boundaryLoop(group))
    {
      loop.push_back(vertices[index]);
    }

    SupportFace face;
    face.corners = withoutStraightCorners(loop);
    if (face.corners.size() < 3 || !(glm::length(summed) > 0.0f))
    {
      return std::nullopt;
    }

    face.normal = glm::normalize(summed);
    face.center = areaCentroid(face.corners, face.normal);

    return face;
  }

  std::vector<SupportFace> hullFacesOf(ConvexMeshCollider& collider)
  {
    const auto& vertices = collider.getWorldVertices();
    const auto triangles = outwardTriangles(vertices, collider.getHull().indices);
    const float tolerance = coplanarDistanceShare * extentOf(vertices);

    std::vector<SupportFace> faces;
    for (const auto& group : groupCoplanar(triangles, vertices, tolerance))
    {
      if (auto face = faceOfGroup(group, vertices))
      {
        faces.push_back(std::move(*face));
      }
    }

    return faces;
  }

  glm::vec3 nearestOnSegment(const glm::vec3& a, const glm::vec3& b, const glm::vec3& point)
  {
    const auto segment = b - a;
    const float lengthSquared = glm::dot(segment, segment);
    const float t = lengthSquared > 0.0f ? std::clamp(glm::dot(point - a, segment) / lengthSquared, 0.0f, 1.0f) : 0.0f;

    return a + t * segment;
  }

  bool insideFace(const SupportFace& face, const glm::vec3& point)
  {
    for (size_t i = 0; i < face.corners.size(); ++i)
    {
      const auto& a = face.corners[i];
      const auto& b = face.corners[(i + 1) % face.corners.size()];
      if (glm::dot(glm::cross(b - a, point - a), face.normal) < 0.0f)
      {
        return false;
      }
    }

    return true;
  }
}

namespace collisions {
  std::vector<SupportFace> supportFacesOf(Collider& collider)
  {
    switch (collider.getColliderType())
    {
      case ColliderType::boxCollider:
        return boxFacesOf(collider);
      case ColliderType::convexMeshCollider:
        return hullFacesOf(dynamic_cast<ConvexMeshCollider&>(collider));
      case ColliderType::sphereCollider:
      case ColliderType::capsuleCollider:
        break;
    }

    return {};
  }

  const SupportFace* faceToward(const std::span<const SupportFace> faces, const glm::vec3& direction)
  {
    const SupportFace* best = nullptr;
    float bestAlignment = std::numeric_limits<float>::lowest();
    for (const auto& face : faces)
    {
      if (const float alignment = glm::dot(face.normal, direction); alignment > bestAlignment)
      {
        best = &face;
        bestAlignment = alignment;
      }
    }

    return best;
  }

  glm::vec3 clampToFace(const SupportFace& face, const glm::vec3& point)
  {
    const float height = glm::dot(point - face.center, face.normal);
    const auto inPlane = point - height * face.normal;
    if (face.corners.size() < 3 || insideFace(face, inPlane))
    {
      return point;
    }

    glm::vec3 nearest = face.corners.front();
    float nearestDistance = std::numeric_limits<float>::max();
    for (size_t i = 0; i < face.corners.size(); ++i)
    {
      const auto candidate = nearestOnSegment(face.corners[i], face.corners[(i + 1) % face.corners.size()], inPlane);
      if (const float distance = glm::dot(candidate - inPlane, candidate - inPlane); distance < nearestDistance)
      {
        nearest = candidate;
        nearestDistance = distance;
      }
    }

    return nearest + height * face.normal;
  }
}
