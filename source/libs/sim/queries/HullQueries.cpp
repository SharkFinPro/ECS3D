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
  }

  bool rayHull(const glm::vec3& origin, const glm::vec3& dir, const std::span<const glm::vec3> vertices,
               const std::span<const uint32_t> indices, const float maxDistance, float& tHit, glm::vec3& normal)
  {
    float tEnter = std::numeric_limits<float>::lowest();
    float tExit = std::numeric_limits<float>::max();
    std::optional<glm::vec3> enteredNormal;

    for (size_t triangle = 0; triangle < indices.size() / 3; ++triangle)
    {
      const auto plane = planeOf(vertices, indices, triangle);
      if (!plane)
      {
        continue;
      }

      const float distance = glm::dot(plane->normal, origin) - plane->offset;
      const float approach = glm::dot(plane->normal, dir);

      if (std::abs(approach) < kEpsilon)
      {
        // Parallel to this face: a miss when the ray lies outside its plane.
        if (distance > 0.0f)
        {
          return false;
        }

        continue;
      }

      const float t = -distance / approach;

      if (approach < 0.0f)
      {
        if (t > tEnter)
        {
          tEnter = t;
          enteredNormal = plane->normal;
        }
      }
      else
      {
        tExit = std::min(tExit, t);
      }

      if (tEnter > tExit)
      {
        return false;
      }
    }

    if (!enteredNormal || tExit < 0.0f)
    {
      return false;
    }

    const float t = std::max(tEnter, 0.0f);
    if (t > maxDistance)
    {
      return false;
    }

    tHit = t;
    normal = *enteredNormal;

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
    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    const glm::vec3 ap = p - a;
    const float d1 = glm::dot(ab, ap);
    const float d2 = glm::dot(ac, ap);

    if (d1 <= 0.0f && d2 <= 0.0f)
    {
      return a;
    }

    const glm::vec3 bp = p - b;
    const float d3 = glm::dot(ab, bp);
    const float d4 = glm::dot(ac, bp);

    if (d3 >= 0.0f && d4 <= d3)
    {
      return b;
    }

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
    {
      return a + ab * (d1 / (d1 - d3));
    }

    const glm::vec3 cp = p - c;
    const float d5 = glm::dot(ab, cp);
    const float d6 = glm::dot(ac, cp);

    if (d6 >= 0.0f && d5 <= d6)
    {
      return c;
    }

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
    {
      return a + ac * (d2 / (d2 - d6));
    }

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
    {
      return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    }

    const float denominator = 1.0f / (va + vb + vc);

    return a + ab * (vb * denominator) + ac * (vc * denominator);
  }
}
