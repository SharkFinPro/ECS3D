#include "SupportFaces.h"
#include "PlanarHull.h"
#include <objects/components/collisions/Collider.h>
#include <objects/components/collisions/ConvexMeshCollider.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>
#include <glm/trigonometric.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace {
  using collisions::SupportFace;

  // Triangles whose normals agree to about a quarter of a degree and whose vertices lie within this share of
  // the hull's size of a face's plane are one face.
  constexpr float coplanarCosine = 1.0f - 1e-5f;
  constexpr float coplanarDistanceShare = 1e-4f;

  // A triangle whose doubled area is below this share of its longest edge squared is a sliver or needle:
  // its normal is not stable, so it joins a face by its vertices' distance to the plane instead.
  constexpr float reliableAreaShare = 1e-2f;

  struct BoxFrame {
    glm::vec3 center;
    glm::mat3 axes;
    glm::vec3 halfExtents;
  };

  BoxFrame boxFrameOf(Collider& collider)
  {
    return { collider.getPosition(), glm::mat3_cast(glm::quat(glm::radians(collider.getRotation()))),
             glm::abs(collider.getScale()) };
  }

  SupportFace boxFace(const BoxFrame& box, const int axis, const float sign)
  {
    const int u = (axis + 1) % 3;
    const int v = (axis + 2) % 3;
    const auto alongU = box.axes[u] * box.halfExtents[u];
    const auto alongV = box.axes[v] * box.halfExtents[v];

    SupportFace face;
    face.normal = box.axes[axis] * sign;
    face.center = box.center + face.normal * box.halfExtents[axis];
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

    return face;
  }

  std::vector<SupportFace> boxFacesOf(Collider& collider)
  {
    const auto box = boxFrameOf(collider);

    std::vector<SupportFace> faces;
    for (int axis = 0; axis < 3; ++axis)
    {
      for (const float sign : { 1.0f, -1.0f })
      {
        faces.push_back(boxFace(box, axis, sign));
      }
    }

    return faces;
  }

  SupportFace boxFaceToward(Collider& collider, const glm::vec3& direction)
  {
    const auto box = boxFrameOf(collider);

    int nearest = 0;
    for (int axis = 1; axis < 3; ++axis)
    {
      if (std::fabs(glm::dot(box.axes[axis], direction)) > std::fabs(glm::dot(box.axes[nearest], direction)))
      {
        nearest = axis;
      }
    }

    return boxFace(box, nearest, glm::dot(box.axes[nearest], direction) < 0.0f ? -1.0f : 1.0f);
  }

  using Vertices = std::vector<glm::vec3>;

  struct Triangle {
    std::array<uint32_t, 3> v{};
    glm::vec3 cross{ 0.0f };
    glm::vec3 normal{ 0.0f };
    float doubledArea = 0.0f;
    bool reliable = false;
  };

  glm::vec3 centroidOf(const Vertices& vertices)
  {
    glm::vec3 centroid{ 0.0f };
    for (const auto& vertex : vertices)
    {
      centroid += vertex;
    }

    return centroid / static_cast<float>(vertices.size());
  }

  // Triangles of the hull in its own space. A reliable one is wound outward from the centroid, whatever the
  // input winding.
  std::vector<Triangle> trianglesOf(const Vertices& vertices, const std::vector<uint32_t>& indices)
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
      triangle.doubledArea = glm::length(triangle.cross);
      const float longest = std::max({ glm::dot(b - a, b - a), glm::dot(c - b, c - b), glm::dot(a - c, a - c) });
      triangle.reliable = triangle.doubledArea > reliableAreaShare * longest;

      if (triangle.reliable)
      {
        if (glm::dot(triangle.cross, (a + b + c) / 3.0f - centroid) < 0.0f)
        {
          std::swap(triangle.v[1], triangle.v[2]);
          triangle.cross = -triangle.cross;
        }

        triangle.normal = triangle.cross / triangle.doubledArea;
      }

      triangles.push_back(triangle);
    }

    return triangles;
  }

  float extentOf(const Vertices& vertices)
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

  struct Group {
    glm::vec3 normal{ 0.0f };
    glm::vec3 seedPoint{ 0.0f };
    glm::vec3 summed{ 0.0f };
    std::vector<uint32_t> vertexIds;
  };

  struct Tolerance {
    const Vertices& vertices;
    float distance;
  };

  bool onPlane(const Group& group, const Tolerance& tolerance, const uint32_t id)
  {
    return std::fabs(glm::dot(group.normal, tolerance.vertices[id] - group.seedPoint)) <= tolerance.distance;
  }

  bool allOnPlane(const Group& group, const Tolerance& tolerance, const Triangle& triangle)
  {
    return std::ranges::all_of(triangle.v, [&](const uint32_t id) { return onPlane(group, tolerance, id); });
  }

  void addTo(Group& group, const Triangle& triangle)
  {
    group.vertexIds.insert(group.vertexIds.end(), triangle.v.begin(), triangle.v.end());
  }

  // Reliable triangles, largest first, seed the groups. The rest join the first group they lie in the
  // plane of, or are dropped.
  std::vector<Group> groupCoplanar(const std::vector<Triangle>& triangles, const Tolerance& tolerance)
  {
    std::vector<const Triangle*> order;
    for (const auto& triangle : triangles)
    {
      if (triangle.reliable)
      {
        order.push_back(&triangle);
      }
    }
    std::ranges::stable_sort(order, [](const Triangle* a, const Triangle* b) { return a->doubledArea > b->doubledArea; });

    std::vector<Group> groups;
    for (const auto* triangle : order)
    {
      const auto joins = [&](const Group& group)
      {
        return glm::dot(group.normal, triangle->normal) >= coplanarCosine && allOnPlane(group, tolerance, *triangle);
      };

      auto group = std::ranges::find_if(groups, joins);
      if (group == groups.end())
      {
        groups.push_back({ triangle->normal, tolerance.vertices[triangle->v[0]], glm::vec3(0.0f), {} });
        group = groups.end() - 1;
      }

      group->summed += triangle->cross;
      addTo(*group, *triangle);
    }

    for (const auto& triangle : triangles)
    {
      const auto group = std::ranges::find_if(groups, [&](const Group& candidate) {
        return !triangle.reliable && allOnPlane(candidate, tolerance, triangle);
      });

      if (group != groups.end())
      {
        addTo(*group, triangle);
      }
    }

    return groups;
  }

  // The convex outline of a group's vertices in its plane, counter-clockwise about its normal, as vertex ids.
  std::vector<uint32_t> outlineOf(const Group& group, const Vertices& vertices)
  {
    auto ids = group.vertexIds;
    std::ranges::sort(ids);
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

    Vertices points;
    for (const auto id : ids)
    {
      points.push_back(vertices[id]);
    }

    std::vector<uint32_t> outline;
    for (const auto index : collisions::convexOutlineIndices(points, group.normal))
    {
      outline.push_back(ids[index]);
    }

    return outline;
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

  // A face from hull vertex ids, placed with the world vertices. The affine map keeps the face planar; it may
  // reverse the winding, which the hull's centroid corrects.
  std::optional<SupportFace> worldFace(const std::vector<uint32_t>& outline, const Vertices& world,
                                       const glm::vec3& hullCentroid)
  {
    if (outline.size() < 3)
    {
      return std::nullopt;
    }

    SupportFace face;
    for (const auto id : outline)
    {
      face.corners.push_back(world[id]);
    }

    glm::vec3 summed{ 0.0f };
    for (size_t i = 1; i + 1 < face.corners.size(); ++i)
    {
      summed += glm::cross(face.corners[i] - face.corners[0], face.corners[i + 1] - face.corners[0]);
    }

    if (!(glm::length(summed) > 0.0f))
    {
      return std::nullopt;
    }

    if (glm::dot(summed, face.corners[0] - hullCentroid) < 0.0f)
    {
      std::ranges::reverse(face.corners);
      summed = -summed;
    }

    face.normal = glm::normalize(summed);
    face.center = areaCentroid(face.corners, face.normal);

    return face;
  }

  std::vector<SupportFace> hullFacesOf(ConvexMeshCollider& collider)
  {
    const auto& hull = collider.getHull();
    const auto& world = collider.getWorldVertices();
    const auto triangles = trianglesOf(hull.vertices, hull.indices);
    const Tolerance tolerance{ hull.vertices, coplanarDistanceShare * extentOf(hull.vertices) };
    const auto hullCentroid = centroidOf(world);

    std::vector<SupportFace> faces;
    for (const auto& group : groupCoplanar(triangles, tolerance))
    {
      if (auto face = worldFace(outlineOf(group, hull.vertices), world, hullCentroid))
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

  std::optional<SupportFace> supportFaceToward(Collider& collider, const glm::vec3& direction)
  {
    if (collider.getColliderType() == ColliderType::boxCollider)
    {
      return boxFaceToward(collider, direction);
    }

    const auto faces = supportFacesOf(collider);
    if (const auto* face = faceToward(faces, direction))
    {
      return *face;
    }

    return std::nullopt;
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
