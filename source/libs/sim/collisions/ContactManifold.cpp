#include "ContactManifold.h"
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace collisions {
  namespace {
    constexpr float dedupeDistance = 1e-4f;

    // A reference polygon thinner than this fraction of its squared radius has no usable edge planes.
    constexpr float minPolygonAreaRatio = 1e-5f;

    constexpr float segmentEpsilon = 1e-12f;

    struct Candidate {
      glm::vec3 point;
      float depth;
    };

    [[nodiscard]] float featureEpsilon(const std::span<const glm::vec3> vertices)
    {
      glm::vec3 low = vertices.front();
      glm::vec3 high = vertices.front();

      for (const auto& vertex : vertices)
      {
        low = glm::min(low, vertex);
        high = glm::max(high, vertex);
      }

      const glm::vec3 half = (high - low) * 0.5f;

      return 1e-3f * std::max({ half.x, half.y, half.z, 1.0f });
    }

    [[nodiscard]] glm::vec3 centroidOf(const std::span<const glm::vec3> points)
    {
      glm::vec3 sum{ 0 };

      for (const auto& point : points)
      {
        sum += point;
      }

      return sum / static_cast<float>(points.size());
    }

    [[nodiscard]] bool containsNear(const std::vector<glm::vec3>& points, const glm::vec3& point)
    {
      return std::ranges::any_of(points, [&point](const glm::vec3& existing) {
        return glm::distance(existing, point) < dedupeDistance;
      });
    }

    // Counter-clockwise about normal, so an edge's inward side is cross(normal, edge).
    [[nodiscard]] std::vector<glm::vec3> orderedPolygon(const std::span<const glm::vec3> feature,
                                                        const glm::vec3& normal)
    {
      std::vector<glm::vec3> unique;
      for (const auto& point : feature)
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

      const glm::vec3 center = centroidOf(unique);
      const glm::vec3 seed = std::fabs(normal.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
      const glm::vec3 u = glm::normalize(glm::cross(normal, seed));
      const glm::vec3 v = glm::cross(normal, u);

      std::vector<std::pair<float, glm::vec3>> keyed;
      keyed.reserve(unique.size());
      for (const auto& point : unique)
      {
        const glm::vec3 offset = point - center;
        keyed.emplace_back(std::atan2(glm::dot(offset, v), glm::dot(offset, u)), point);
      }

      std::ranges::stable_sort(keyed, [](const auto& first, const auto& second) {
        return first.first < second.first;
      });

      std::vector<glm::vec3> ordered;
      ordered.reserve(keyed.size());
      for (const auto& entry : keyed)
      {
        ordered.push_back(entry.second);
      }

      return ordered;
    }

    [[nodiscard]] bool hasUsableArea(const std::vector<glm::vec3>& polygon, const glm::vec3& normal)
    {
      if (polygon.size() < 3)
      {
        return false;
      }

      const glm::vec3 center = centroidOf(polygon);

      float doubleArea = 0.0f;
      float radiusSquared = 0.0f;
      for (size_t i = 0; i < polygon.size(); ++i)
      {
        const glm::vec3 current = polygon[i] - center;
        const glm::vec3 next = polygon[(i + 1) % polygon.size()] - center;

        doubleArea += glm::dot(glm::cross(current, next), normal);
        radiusSquared = std::max(radiusSquared, glm::dot(current, current));
      }

      return doubleArea * 0.5f > minPolygonAreaRatio * radiusSquared;
    }

    // One Sutherland-Hodgman pass against the half-space dot(inward, x - origin) >= 0.
    [[nodiscard]] std::vector<glm::vec3> clipAgainstPlane(const std::vector<glm::vec3>& input,
                                                          const glm::vec3& origin, const glm::vec3& inward)
    {
      std::vector<glm::vec3> output;

      for (size_t i = 0; i < input.size(); ++i)
      {
        const glm::vec3& current = input[i];
        const glm::vec3& previous = input[(i + input.size() - 1) % input.size()];

        const float currentDistance = glm::dot(inward, current - origin);
        const float previousDistance = glm::dot(inward, previous - origin);
        const bool currentInside = currentDistance >= 0.0f;
        const bool previousInside = previousDistance >= 0.0f;

        if (currentInside != previousInside)
        {
          const float t = previousDistance / (previousDistance - currentDistance);
          output.push_back(previous + (current - previous) * t);
        }

        if (currentInside)
        {
          output.push_back(current);
        }
      }

      return output;
    }

    [[nodiscard]] std::vector<glm::vec3> clipToPolygon(std::vector<glm::vec3> subject,
                                                       const std::vector<glm::vec3>& polygon,
                                                       const glm::vec3& normal)
    {
      for (size_t i = 0; i < polygon.size() && !subject.empty(); ++i)
      {
        const glm::vec3 edge = polygon[(i + 1) % polygon.size()] - polygon[i];
        const float length = glm::length(edge);

        if (length < dedupeDistance)
        {
          continue;
        }

        subject = clipAgainstPlane(subject, polygon[i], glm::cross(normal, edge / length));
      }

      return subject;
    }

    // The nearest points of segments (p1, q1) and (p2, q2); either may be a single point.
    [[nodiscard]] std::pair<glm::vec3, glm::vec3> closestSegmentPoints(const glm::vec3& p1, const glm::vec3& q1,
                                                                       const glm::vec3& p2, const glm::vec3& q2)
    {
      const glm::vec3 d1 = q1 - p1;
      const glm::vec3 d2 = q2 - p2;
      const glm::vec3 r = p1 - p2;
      const float a = glm::dot(d1, d1);
      const float e = glm::dot(d2, d2);
      const float f = glm::dot(d2, r);

      float s = 0.0f;
      float t = 0.0f;

      if (a <= segmentEpsilon && e <= segmentEpsilon)
      {
        return { p1, p2 };
      }

      if (a <= segmentEpsilon)
      {
        t = std::clamp(f / e, 0.0f, 1.0f);
      }
      else
      {
        const float c = glm::dot(d1, r);

        if (e <= segmentEpsilon)
        {
          s = std::clamp(-c / a, 0.0f, 1.0f);
        }
        else
        {
          const float b = glm::dot(d1, d2);
          const float denominator = a * e - b * b;

          s = denominator > segmentEpsilon ? std::clamp((b * f - c * e) / denominator, 0.0f, 1.0f) : 0.0f;
          t = (b * s + f) / e;

          if (t < 0.0f)
          {
            t = 0.0f;
            s = std::clamp(-c / a, 0.0f, 1.0f);
          }
          else if (t > 1.0f)
          {
            t = 1.0f;
            s = std::clamp((b - c) / a, 0.0f, 1.0f);
          }
        }
      }

      return { p1 + d1 * s, p2 + d2 * t };
    }

    // A feature with no area: its first point and the one furthest from it.
    [[nodiscard]] std::pair<glm::vec3, glm::vec3> segmentOf(const std::span<const glm::vec3> feature)
    {
      glm::vec3 farthest = feature.front();
      float farDistance = 0.0f;

      for (const auto& point : feature)
      {
        if (const float distance = glm::distance(point, feature.front()); distance > farDistance)
        {
          farDistance = distance;
          farthest = point;
        }
      }

      return { feature.front(), farthest };
    }

    [[nodiscard]] glm::vec3 midwayPoint(const std::span<const glm::vec3> featureA,
                                        const std::span<const glm::vec3> featureB)
    {
      const auto [startA, endA] = segmentOf(featureA);
      const auto [startB, endB] = segmentOf(featureB);
      const auto [closestA, closestB] = closestSegmentPoints(startA, endA, startB, endB);

      return (closestA + closestB) * 0.5f;
    }

    // The deepest candidate, then repeatedly the one furthest from everything chosen so far.
    [[nodiscard]] std::vector<glm::vec3> spreadOut(const std::vector<Candidate>& candidates, const size_t maxPoints)
    {
      std::vector<glm::vec3> chosen;
      if (candidates.size() <= maxPoints)
      {
        for (const auto& candidate : candidates)
        {
          chosen.push_back(candidate.point);
        }

        return chosen;
      }

      size_t deepest = 0;
      for (size_t i = 1; i < candidates.size(); ++i)
      {
        if (candidates[i].depth > candidates[deepest].depth)
        {
          deepest = i;
        }
      }

      chosen.push_back(candidates[deepest].point);

      while (chosen.size() < maxPoints)
      {
        size_t best = 0;
        float bestSeparation = -1.0f;

        for (size_t i = 0; i < candidates.size(); ++i)
        {
          float separation = std::numeric_limits<float>::max();
          for (const auto& picked : chosen)
          {
            separation = std::min(separation, glm::distance(candidates[i].point, picked));
          }

          if (separation > bestSeparation)
          {
            bestSeparation = separation;
            best = i;
          }
        }

        chosen.push_back(candidates[best].point);
      }

      return chosen;
    }
  }

  std::vector<glm::vec3> touchingFeature(const std::span<const glm::vec3> vertices, const glm::vec3& direction)
  {
    if (vertices.empty())
    {
      return {};
    }

    float extreme = std::numeric_limits<float>::lowest();
    for (const auto& vertex : vertices)
    {
      extreme = std::max(extreme, glm::dot(vertex, direction));
    }

    const float epsilon = featureEpsilon(vertices);

    std::vector<glm::vec3> selected;
    for (const auto& vertex : vertices)
    {
      if (glm::dot(vertex, direction) >= extreme - epsilon)
      {
        selected.push_back(vertex);
      }
    }

    return selected;
  }

  std::vector<glm::vec3> clipManifold(const std::span<const glm::vec3> featureA,
                                      const std::span<const glm::vec3> featureB,
                                      const glm::vec3& normal, const size_t maxPoints)
  {
    if (featureA.empty() || featureB.empty())
    {
      const auto& present = featureA.empty() ? featureB : featureA;

      return std::vector<glm::vec3>(present.begin(), present.end());
    }

    const bool aIsReference = featureA.size() >= featureB.size();
    const auto reference = aIsReference ? featureA : featureB;
    const auto incident = aIsReference ? featureB : featureA;

    const auto polygon = orderedPolygon(reference, normal);
    if (!hasUsableArea(polygon, normal))
    {
      return { midwayPoint(featureA, featureB) };
    }

    auto clipped = clipToPolygon(std::vector<glm::vec3>(incident.begin(), incident.end()), polygon, normal);
    if (clipped.empty())
    {
      clipped.assign(incident.begin(), incident.end());
    }

    const glm::vec3 planePoint = centroidOf(polygon);
    const glm::vec3 outward = aIsReference ? normal : -normal;

    std::vector<Candidate> candidates;
    std::vector<glm::vec3> projectedPoints;
    for (const auto& point : clipped)
    {
      const glm::vec3 projected = point - normal * glm::dot(point - planePoint, normal);

      if (!containsNear(projectedPoints, projected))
      {
        projectedPoints.push_back(projected);
        candidates.push_back({ projected, -glm::dot(point - planePoint, outward) });
      }
    }

    return spreadOut(candidates, std::max<size_t>(maxPoints, 1));
  }
}
