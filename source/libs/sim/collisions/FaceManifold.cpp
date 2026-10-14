#include "FaceManifold.h"
#include "ContactManifold.h"
#include "ManifoldInternal.h"
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace collisions {
  using detail::Candidate;
  using detail::centroidOf;
  using detail::closestPointOnSegment;
  using detail::containsNear;
  using detail::segmentEpsilon;
  using detail::spreadOut;

  namespace {
    // A triangle whose cross product is this small against its two edges is a sliver of rounding noise.
    constexpr float collinearRatio = 1e-6f;

    struct AlignedFace {
      glm::vec3 normal;
      float alignment;
    };

    [[nodiscard]] AlignedFace mostAlignedFace(const std::span<const glm::vec3> normals, const glm::vec3& direction)
    {
      AlignedFace best{ normals.front(), glm::dot(normals.front(), direction) };

      for (const auto& candidate : normals.subspan(1))
      {
        if (const float alignment = glm::dot(candidate, direction); alignment > best.alignment)
        {
          best = { candidate, alignment };
        }
      }

      return best;
    }

    // The convex hull of the points as seen along normal, counter-clockwise about it, so interior and
    // collinear points of a faceted face drop out. Fewer than three distinct points come back as they are.
    [[nodiscard]] std::vector<glm::vec3> convexPolygon(const std::span<const glm::vec3> points,
                                                       const glm::vec3& normal)
    {
      std::vector<glm::vec3> unique;
      for (const auto& point : points)
      {
        if (!containsNear(unique, point))
        {
          unique.push_back(point);
        }
      }

      if (unique.size() < 3)
      {
        return unique;
      }

      const glm::vec3 seed = std::fabs(normal.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
      const glm::vec3 u = glm::normalize(glm::cross(normal, seed));
      const glm::vec3 v = glm::cross(normal, u);

      const auto planar = [&](const glm::vec3& point) {
        return glm::vec2(glm::dot(point, u), glm::dot(point, v));
      };
      const auto turn = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
        const glm::vec2 pa = planar(a);
        const glm::vec2 ab = planar(b) - pa;
        const glm::vec2 ac = planar(c) - pa;

        return ab.x * ac.y - ab.y * ac.x;
      };

      std::ranges::sort(unique, [&](const glm::vec3& a, const glm::vec3& b) {
        const glm::vec2 pa = planar(a);
        const glm::vec2 pb = planar(b);

        return pa.x < pb.x || (pa.x == pb.x && pa.y < pb.y);
      });

      const size_t count = unique.size();
      std::vector<glm::vec3> hull(2 * count);
      size_t size = 0;

      for (size_t i = 0; i < count; ++i)
      {
        while (size >= 2 && turn(hull[size - 2], hull[size - 1], unique[i]) <= 0.0f)
        {
          --size;
        }

        hull[size++] = unique[i];
      }

      for (size_t i = count - 1, lowest = size + 1; i-- > 0;)
      {
        while (size >= lowest && turn(hull[size - 2], hull[size - 1], unique[i]) <= 0.0f)
        {
          --size;
        }

        hull[size++] = unique[i];
      }

      hull.resize(size - 1);

      return hull;
    }

    // The point of the convex polygon (counter-clockwise about normal, in the plane of point) nearest to point.
    // A polygon of fewer than three points is all boundary.
    [[nodiscard]] glm::vec3 clampToPolygon(const glm::vec3& point, const std::vector<glm::vec3>& polygon,
                                           const glm::vec3& normal)
    {
      bool inside = polygon.size() >= 3;
      glm::vec3 nearest = polygon.front();
      float nearestDistance = std::numeric_limits<float>::max();

      for (size_t i = 0; i < polygon.size(); ++i)
      {
        const glm::vec3& start = polygon[i];
        const glm::vec3& end = polygon[(i + 1) % polygon.size()];
        const glm::vec3 edge = end - start;

        if (glm::dot(glm::cross(normal, edge), point - start) < 0.0f)
        {
          inside = false;
        }

        const glm::vec3 closest = glm::dot(edge, edge) > segmentEpsilon ? closestPointOnSegment(point, start, end)
                                                                         : start;

        if (const float distance = glm::distance(point, closest); distance < nearestDistance)
        {
          nearestDistance = distance;
          nearest = closest;
        }
      }

      return inside ? point : nearest;
    }
  }

  std::vector<glm::vec3> hullFaceNormals(const std::span<const glm::vec3> vertices,
                                         const std::span<const uint32_t> indices)
  {
    if (vertices.empty())
    {
      return {};
    }

    // A mirrored transform reverses every triangle's winding, so each normal is turned away from the center.
    const glm::vec3 center = centroidOf(vertices);

    std::vector<glm::vec3> normals;
    for (size_t i = 0; i + 2 < indices.size(); i += 3)
    {
      const glm::vec3& a = vertices[indices[i]];
      const glm::vec3 first = vertices[indices[i + 1]] - a;
      const glm::vec3 second = vertices[indices[i + 2]] - a;
      const glm::vec3 normal = glm::cross(first, second);

      if (const float length = glm::length(normal); length > collinearRatio * glm::length(first) * glm::length(second))
      {
        const glm::vec3 unit = normal / length;
        normals.push_back(glm::dot(unit, center - a) > 0.0f ? -unit : unit);
      }
    }

    return normals;
  }

  std::vector<glm::vec3> referenceFaceManifold(const FaceShape& first, const FaceShape& second,
                                               const glm::vec3& normal, const size_t maxPoints)
  {
    if (first.faceNormals.empty() || second.faceNormals.empty())
    {
      return {};
    }

    const AlignedFace faceFirst = mostAlignedFace(first.faceNormals, normal);
    const AlignedFace faceSecond = mostAlignedFace(second.faceNormals, -normal);

    const bool firstIsReference = faceFirst.alignment >= faceSecond.alignment - faceAlignmentTie;
    const auto& reference = firstIsReference ? first : second;
    const auto& incident = firstIsReference ? second : first;
    const glm::vec3 faceNormal = firstIsReference ? faceFirst.normal : faceSecond.normal;

    const auto polygon = convexPolygon(touchingFeature(reference.vertices, faceNormal, reference.featureEpsilon),
                                       faceNormal);
    if (polygon.empty())
    {
      return {};
    }

    const glm::vec3 planePoint = centroidOf(polygon);
    const glm::vec3 towardReference = firstIsReference ? -normal : normal;

    std::vector<Candidate> candidates;
    std::vector<glm::vec3> placed;
    for (const auto& vertex : touchingFeature(incident.vertices, towardReference, incident.featureEpsilon))
    {
      const float offset = glm::dot(vertex - planePoint, faceNormal);
      const glm::vec3 point = clampToPolygon(vertex - faceNormal * offset, polygon, faceNormal) + faceNormal * offset;

      if (!containsNear(placed, point))
      {
        placed.push_back(point);
        candidates.push_back({ point, -offset });
      }
    }

    return spreadOut(candidates, std::max<size_t>(maxPoints, 1));
  }
}
