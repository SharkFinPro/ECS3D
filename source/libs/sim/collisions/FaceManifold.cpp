#include "FaceManifold.h"
#include "ContactManifold.h"
#include "ManifoldInternal.h"
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <algorithm>
#include <cmath>
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

    // Twice the area of the triangle a, b, c as seen along normal, signed by its turn.
    [[nodiscard]] float turnArea(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& normal)
    {
      return glm::dot(glm::cross(b - a, c - a), normal);
    }

    // At most four of the candidates: the deepest, which the push out of the overlap leans on, and the three that
    // make its polygon cover the most area. The candidates are the corners of a convex loop in order, so the polygon
    // through chosen ones is a fan of triangles from the deepest. Any other number of points is spread by depth and
    // distance.
    [[nodiscard]] std::vector<glm::vec3> largestSpread(const std::vector<Candidate>& candidates,
                                                       const glm::vec3& normal, const size_t maxPoints)
    {
      constexpr size_t keptPoints = 4;
      if (candidates.size() <= maxPoints || maxPoints != keptPoints)
      {
        return spreadOut(candidates, maxPoints);
      }

      const size_t count = candidates.size();
      size_t deepest = 0;
      for (size_t i = 1; i < count; ++i)
      {
        if (candidates[i].depth > candidates[deepest].depth)
        {
          deepest = i;
        }
      }

      // The loop from the deepest corner on, so the others are indices 1 to count - 1 in order.
      std::vector<glm::vec3> loop;
      loop.reserve(count);
      for (size_t i = 0; i < count; ++i)
      {
        loop.push_back(candidates[(deepest + i) % count].point);
      }

      size_t bestFirst = 1;
      size_t bestSecond = 2;
      size_t bestThird = 3;
      float bestArea = -1.0f;
      for (size_t first = 1; first + 2 < count; ++first)
      {
        for (size_t second = first + 1; second + 1 < count; ++second)
        {
          const float firstTurn = turnArea(loop[0], loop[first], loop[second], normal);
          for (size_t third = second + 1; third < count; ++third)
          {
            const float area = std::fabs(firstTurn + turnArea(loop[0], loop[second], loop[third], normal));
            if (area > bestArea)
            {
              bestArea = area;
              bestFirst = first;
              bestSecond = second;
              bestThird = third;
            }
          }
        }
      }

      return { loop[0], loop[bestFirst], loop[bestSecond], loop[bestThird] };
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
    std::vector<glm::vec3> points;
    for (const auto& contact : referenceFaceContacts(first, second, normal, maxPoints, 0.0f))
    {
      points.push_back(contact.point);
    }

    return points;
  }

  std::optional<glm::vec3> referenceFaceNormal(const FaceShape& first, const FaceShape& second,
                                               const glm::vec3& normal)
  {
    if (first.faceNormals.empty() || second.faceNormals.empty())
    {
      return std::nullopt;
    }

    // Cosine of about a degree.
    constexpr float faceOnCosine = 0.9998f;

    const AlignedFace faceFirst = mostAlignedFace(first.faceNormals, normal);
    const AlignedFace faceSecond = mostAlignedFace(second.faceNormals, -normal);
    const bool firstIsReference = faceFirst.alignment >= faceSecond.alignment - faceAlignmentTie;
    const auto& face = firstIsReference ? faceFirst : faceSecond;
    if (face.alignment < faceOnCosine)
    {
      return std::nullopt;
    }

    return firstIsReference ? face.normal : -face.normal;
  }

  std::vector<ManifoldPoint> referenceFaceContacts(const FaceShape& first, const FaceShape& second,
                                                   const glm::vec3& normal, const size_t maxPoints,
                                                   const float margin)
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

    auto incidentFeature = touchingFeature(incident.vertices, towardReference, incident.featureEpsilon);

    // With a margin, the whole of the other side's face that is turned toward the reference face is part of the
    // contact wherever it is past that face or within the margin of it, however deep the touching vertex is: a
    // face sunk in at an angle then reports all of itself that is inside, not just its deepest edge.
    if (margin > 0.0f)
    {
      const auto facing = mostAlignedFace(incident.faceNormals, towardReference).normal;
      for (const auto& vertex : touchingFeature(incident.vertices, facing, incident.featureEpsilon))
      {
        if (glm::dot(vertex - planePoint, faceNormal) <= margin && !containsNear(incidentFeature, vertex))
        {
          incidentFeature.push_back(vertex);
        }
      }
    }

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
      overlap = detail::clipAgainstPlane(overlap, planePoint, -faceNormal,
                                         std::max(reference.featureEpsilon, margin));
      for (const auto& point : overlap)
      {
        place(point, glm::dot(point - planePoint, faceNormal));
      }
    }

    // A corner, or an edge or face wholly outside the reference polygon along the plane, still touches somewhere: its
    // vertices are pulled onto the reference polygon, keeping their depth, and put in loop order.
    if (candidates.empty())
    {
      std::vector<glm::vec3> clamped;
      for (const auto& vertex : incidentFeature)
      {
        const float offset = glm::dot(vertex - planePoint, faceNormal);
        clamped.push_back(clampToPolygon(vertex - faceNormal * offset, polygon, faceNormal) + faceNormal * offset);
      }

      for (const auto& point : convexPolygon(clamped, faceNormal))
      {
        place(point, glm::dot(point - planePoint, faceNormal));
      }
    }

    std::vector<ManifoldPoint> contacts;
    for (const auto& point : largestSpread(candidates, faceNormal, std::max<size_t>(maxPoints, 1)))
    {
      const auto chosen = std::ranges::find(candidates, point, &Candidate::point);
      contacts.push_back({ point, chosen->depth });
    }

    return contacts;
  }
}
