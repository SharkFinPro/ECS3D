#include "FaceManifold.h"
#include "ContactManifold.h"
#include "ManifoldInternal.h"
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

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

    // Corner sets this small are searched exhaustively for the choice covering the most area.
    constexpr size_t maxSearchedCandidates = 12;

    // The area of the points' convex hull along normal; the points of a convex loop taken in order are their own hull.
    [[nodiscard]] float loopArea(const std::vector<glm::vec3>& points, const std::vector<size_t>& chosen,
                                 const glm::vec3& normal)
    {
      const glm::vec3 origin = points[chosen.front()];
      glm::vec3 sum{ 0 };
      for (size_t i = 0; i < chosen.size(); ++i)
      {
        sum += glm::cross(points[chosen[i]] - origin, points[chosen[(i + 1) % chosen.size()]] - origin);
      }

      return std::fabs(glm::dot(sum, normal)) * 0.5f;
    }

    // At most maxPoints of the candidates, the ones whose polygon covers the most area, so the manifold spans as much
    // of the overlap as that many points can. Candidates arrive in loop order around the face. Among equal areas the
    // choice holding the deepest point wins.
    [[nodiscard]] std::vector<glm::vec3> largestSpread(const std::vector<Candidate>& candidates,
                                                       const glm::vec3& normal, const size_t maxPoints)
    {
      if (candidates.size() <= maxPoints || candidates.size() > maxSearchedCandidates || maxPoints < 3)
      {
        return spreadOut(candidates, maxPoints);
      }

      std::vector<glm::vec3> points;
      size_t deepest = 0;
      for (size_t i = 0; i < candidates.size(); ++i)
      {
        points.push_back(candidates[i].point);
        if (candidates[i].depth > candidates[deepest].depth)
        {
          deepest = i;
        }
      }

      // Every way to pick maxPoints of the loop, in order, as the bits of a mask.
      std::vector<size_t> best;
      float bestArea = -1.0f;
      for (uint32_t mask = 0; mask < (1u << candidates.size()); ++mask)
      {
        if (static_cast<size_t>(std::popcount(mask)) != maxPoints)
        {
          continue;
        }

        std::vector<size_t> chosen;
        for (size_t i = 0; i < candidates.size(); ++i)
        {
          if ((mask >> i) & 1u)
          {
            chosen.push_back(i);
          }
        }

        const float area = loopArea(points, chosen, normal);
        const bool holdsDeepest = std::ranges::find(chosen, deepest) != chosen.end();
        const bool tied = std::fabs(area - bestArea) <= 1e-5f * std::max(bestArea, 1e-6f);
        const bool bestHoldsDeepest = !best.empty() && std::ranges::find(best, deepest) != best.end();
        if (area > bestArea && !tied)
        {
          best = chosen;
          bestArea = area;
        }
        else if (tied && holdsDeepest && !bestHoldsDeepest)
        {
          best = chosen;
        }
      }

      std::vector<glm::vec3> result;
      for (const size_t index : best)
      {
        result.push_back(points[index]);
      }

      return result;
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

    const auto incidentFeature = touchingFeature(incident.vertices, towardReference, incident.featureEpsilon);

    std::vector<Candidate> candidates;
    std::vector<glm::vec3> placed;
    const auto place = [&](const glm::vec3& point, const float offset)
    {
      if (!containsNear(placed, point))
      {
        placed.push_back(point);
        candidates.push_back({ point, -offset });
      }
    };

    // Two faces overlap in the incident face clipped to the reference one's side planes, and an edge of the incident
    // side the same way. What is left is cut at the reference plane too, so a face tilted a little off it keeps the
    // part that touches. The points stay on the incident side.
    auto incidentLoop = convexPolygon(incidentFeature, faceNormal);
    if (polygon.size() >= 3 && incidentLoop.size() >= 2)
    {
      auto overlap = detail::clipToPolygon(std::move(incidentLoop), polygon, faceNormal);
      overlap = detail::clipAgainstPlane(overlap, planePoint, -faceNormal, reference.featureEpsilon);
      for (const auto& point : overlap)
      {
        place(point, glm::dot(point - planePoint, faceNormal));
      }
    }

    // A corner, or an edge or face wholly outside the reference polygon along the plane, still touches somewhere: its
    // vertices are pulled onto the reference polygon, keeping their depth.
    if (candidates.empty())
    {
      for (const auto& vertex : incidentFeature)
      {
        const float offset = glm::dot(vertex - planePoint, faceNormal);
        place(clampToPolygon(vertex - faceNormal * offset, polygon, faceNormal) + faceNormal * offset, offset);
      }
    }

    return largestSpread(candidates, faceNormal, std::max<size_t>(maxPoints, 1));
  }
}
