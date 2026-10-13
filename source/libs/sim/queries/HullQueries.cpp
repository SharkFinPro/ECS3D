#include "HullQueries.h"
#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace hullQueries {
  namespace {
    constexpr float kEpsilon = 1e-8f;

    // A triangle is degenerate when its area is this small a fraction of its two edge lengths' product,
    // so the test scales with the hull instead of sitting below float noise for a large one.
    constexpr float kDegenerateRatio = 1e-6f;

    struct FacePlane {
      glm::vec3 normal;
      float offset;
    };

    // The part of a ray inside the half-spaces clipped so far.
    struct RaySpan {
      float enter = std::numeric_limits<float>::lowest();
      float exit = std::numeric_limits<float>::max();
      std::optional<glm::vec3> enteredNormal;
      size_t planesClipped = 0;
    };

    [[nodiscard]] glm::vec3 centroidOf(const std::span<const glm::vec3> vertices)
    {
      glm::vec3 sum{ 0.0f };

      for (const auto& vertex : vertices)
      {
        sum += vertex;
      }

      return sum / static_cast<float>(vertices.size());
    }

    // Facing away from inside, whatever the winding: a mirrored transform (an odd number of negative scale
    // axes) flips every triangle, so the normal is turned to point away from the hull's center.
    // nullopt for a triangle too thin to have a normal.
    [[nodiscard]] std::optional<FacePlane> planeOf(const std::span<const glm::vec3> vertices,
                                                   const std::span<const uint32_t> indices, const size_t triangle,
                                                   const glm::vec3& center)
    {
      const glm::vec3& a = vertices[indices[triangle * 3]];
      const glm::vec3& b = vertices[indices[triangle * 3 + 1]];
      const glm::vec3& c = vertices[indices[triangle * 3 + 2]];

      const glm::vec3 cross = glm::cross(b - a, c - a);
      const float length = glm::length(cross);

      if (!(length > kDegenerateRatio * glm::length(b - a) * glm::length(c - a)))
      {
        return std::nullopt;
      }

      glm::vec3 normal = cross / length;
      if (glm::dot(normal, center - a) > 0.0f)
      {
        normal = -normal;
      }

      return FacePlane{ normal, glm::dot(normal, a) };
    }

    [[nodiscard]] bool insideAllPlanes(const glm::vec3& point, const std::span<const glm::vec3> vertices,
                                       const std::span<const uint32_t> indices)
    {
      const glm::vec3 hullCenter = centroidOf(vertices);
      size_t planesTested = 0;

      for (size_t triangle = 0; triangle < indices.size() / 3; ++triangle)
      {
        const auto plane = planeOf(vertices, indices, triangle, hullCenter);

        if (!plane)
        {
          continue;
        }

        ++planesTested;
        if (glm::dot(plane->normal, point) - plane->offset > 0.0f)
        {
          return false;
        }
      }

      // A hull with no usable face (collapsed by a zero scale) has no inside.
      return planesTested > 0;
    }

    // Narrows the span to the plane's inside. False when the ray can no longer be inside the hull.
    [[nodiscard]] bool clipRay(const FacePlane& plane, const glm::vec3& origin, const glm::vec3& dir, RaySpan& span)
    {
      ++span.planesClipped;
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

      if (!(lengthSquared > 0.0f))
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
    const glm::vec3 hullCenter = centroidOf(vertices);

    for (size_t triangle = 0; triangle < indices.size() / 3; ++triangle)
    {
      const auto plane = planeOf(vertices, indices, triangle, hullCenter);

      if (plane && !clipRay(*plane, origin, dir, span))
      {
        return false;
      }
    }

    if (span.planesClipped == 0 || span.exit < 0.0f)
    {
      return false;
    }

    // Started inside: contact at the origin, with the same -dir normal the other shapes report there.
    const bool startsInside = span.enter < 0.0f || !span.enteredNormal;
    const float t = startsInside ? 0.0f : span.enter;
    if (t > maxDistance)
    {
      return false;
    }

    tHit = t;
    normal = startsInside ? -dir : *span.enteredNormal;

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
    const float edgeProductSquared = glm::dot(b - a, b - a) * glm::dot(c - a, c - a);

    if (crossLengthSquared > kDegenerateRatio * kDegenerateRatio * edgeProductSquared && crossLengthSquared > 0.0f)
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
