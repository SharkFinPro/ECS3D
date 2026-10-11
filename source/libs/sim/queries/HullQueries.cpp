#include "HullQueries.h"
#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace hullQueries {
  namespace {
    constexpr float kEpsilon = 1e-8f;

    struct FacePlane {
      glm::vec3 normal;
      float offset;
    };

    // The part of a ray inside the half-spaces clipped so far.
    struct RaySpan {
      float enter = std::numeric_limits<float>::lowest();
      float exit = std::numeric_limits<float>::max();
      std::optional<glm::vec3> enteredNormal;
    };

    // nullopt for a triangle too thin to have a normal.
    [[nodiscard]] std::optional<FacePlane> planeOf(const std::span<const glm::vec3> vertices,
                                                   const std::span<const uint32_t> indices, const size_t triangle)
    {
      const glm::vec3& a = vertices[indices[triangle * 3]];
      const glm::vec3& b = vertices[indices[triangle * 3 + 1]];
      const glm::vec3& c = vertices[indices[triangle * 3 + 2]];

      const glm::vec3 cross = glm::cross(b - a, c - a);
      const float length = glm::length(cross);

      if (!(length > kEpsilon))
      {
        return std::nullopt;
      }

      const glm::vec3 normal = cross / length;

      return FacePlane{ normal, glm::dot(normal, a) };
    }

    [[nodiscard]] bool insideAllPlanes(const glm::vec3& point, const std::span<const glm::vec3> vertices,
                                       const std::span<const uint32_t> indices)
    {
      for (size_t triangle = 0; triangle < indices.size() / 3; ++triangle)
      {
        const auto plane = planeOf(vertices, indices, triangle);

        if (plane && glm::dot(plane->normal, point) - plane->offset > 0.0f)
        {
          return false;
        }
      }

      return true;
    }

    // Narrows the span to the plane's inside. False when the ray can no longer be inside the hull.
    [[nodiscard]] bool clipRay(const FacePlane& plane, const glm::vec3& origin, const glm::vec3& dir, RaySpan& span)
    {
      const float distance = glm::dot(plane.normal, origin) - plane.offset;
      const float approach = glm::dot(plane.normal, dir);

      if (std::abs(approach) < kEpsilon)
      {
        // Parallel to this face: a miss when the ray lies outside its plane.
        return distance <= 0.0f;
      }

      const float t = -distance / approach;

      if (approach < 0.0f && t > span.enter)
      {
        span.enter = t;
        span.enteredNormal = plane.normal;
      }
      else if (approach > 0.0f)
      {
        span.exit = std::min(span.exit, t);
      }

      return span.enter <= span.exit;
    }

    [[nodiscard]] glm::vec3 closestPointOnSegment(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b)
    {
      const glm::vec3 ab = b - a;
      const float lengthSquared = glm::dot(ab, ab);

      if (!(lengthSquared > kEpsilon))
      {
        return a;
      }

      return a + ab * std::clamp(glm::dot(p - a, ab) / lengthSquared, 0.0f, 1.0f);
    }

    // Whether p, which lies in the triangle's plane, is on the inner side of all three edges.
    [[nodiscard]] bool insideTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b,
                                      const glm::vec3& c, const glm::vec3& normal)
    {
      return glm::dot(glm::cross(b - a, p - a), normal) >= 0.0f
          && glm::dot(glm::cross(c - b, p - b), normal) >= 0.0f
          && glm::dot(glm::cross(a - c, p - c), normal) >= 0.0f;
    }
  }

  bool rayHull(const glm::vec3& origin, const glm::vec3& dir, const std::span<const glm::vec3> vertices,
               const std::span<const uint32_t> indices, const float maxDistance, float& tHit, glm::vec3& normal)
  {
    RaySpan span;

    for (size_t triangle = 0; triangle < indices.size() / 3; ++triangle)
    {
      const auto plane = planeOf(vertices, indices, triangle);

      if (plane && !clipRay(*plane, origin, dir, span))
      {
        return false;
      }
    }

    if (!span.enteredNormal || span.exit < 0.0f)
    {
      return false;
    }

    const float t = std::max(span.enter, 0.0f);
    if (t > maxDistance)
    {
      return false;
    }

    tHit = t;
    normal = *span.enteredNormal;

    return true;
  }

  bool sphereOverlapsHull(const glm::vec3& center, const float radius, const std::span<const glm::vec3> vertices,
                          const std::span<const uint32_t> indices)
  {
    if (insideAllPlanes(center, vertices, indices))
    {
      return true;
    }

    for (size_t triangle = 0; triangle < indices.size() / 3; ++triangle)
    {
      const glm::vec3 closest = closestPointOnTriangle(center, vertices[indices[triangle * 3]],
                                                       vertices[indices[triangle * 3 + 1]],
                                                       vertices[indices[triangle * 3 + 2]]);

      if (glm::dot(center - closest, center - closest) <= radius * radius)
      {
        return true;
      }
    }

    return false;
  }

  glm::vec3 closestPointOnTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
  {
    const glm::vec3 cross = glm::cross(b - a, c - a);
    const float crossLengthSquared = glm::dot(cross, cross);

    if (crossLengthSquared > kEpsilon * kEpsilon)
    {
      const glm::vec3 normal = cross / std::sqrt(crossLengthSquared);
      const glm::vec3 projected = p - normal * glm::dot(p - a, normal);

      if (insideTriangle(projected, a, b, c, normal))
      {
        return projected;
      }
    }

    // Outside the face (or a face with no area): the nearest of the three edges.
    glm::vec3 best = closestPointOnSegment(p, a, b);
    for (const glm::vec3& candidate : { closestPointOnSegment(p, b, c), closestPointOnSegment(p, c, a) })
    {
      if (glm::dot(p - candidate, p - candidate) < glm::dot(p - best, p - best))
      {
        best = candidate;
      }
    }

    return best;
  }
}
