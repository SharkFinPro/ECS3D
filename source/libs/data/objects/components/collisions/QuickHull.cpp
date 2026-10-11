#include "QuickHull.h"
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>

namespace {
  // Relative to the point cloud's extent: closer to a plane than this counts as on it.
  constexpr double epsilonScale = 1e-5;

  constexpr double tinyNormalLength = 1e-12;

  // The build is refused once the hull-in-progress has this many faces: it can only shrink back when a later
  // point swallows vertices, and the caller refuses a hull this large anyway.
  constexpr size_t maxAliveFaces = 4 * (2 * maxConvexHullVertices - 4);

  constexpr size_t expansionsPerPoint = 10;

  // How far below a face plane still counts as seeing it. Exactly collinear lattice input otherwise sees one
  // neighbor and misses the other on rounding noise, and the cone built on that horizon has a zero-area face.
  constexpr double visibilityNoiseFactor = 1e-6;

  struct HullFace {
    std::array<uint32_t, 3> v{};
    glm::dvec3 normal{ 0.0 };
    double offset = 0.0;
    std::vector<uint32_t> outside;
    bool alive = true;
  };

  [[nodiscard]] uint64_t edgeKey(const uint32_t from, const uint32_t to)
  {
    return (static_cast<uint64_t>(from) << 32) | to;
  }

  class QuickHullBuilder {
  public:
    explicit QuickHullBuilder(const std::span<const glm::vec3> points)
    {
      m_points.reserve(points.size());
      m_maxExpansions = expansionsPerPoint * points.size();

      for (const auto& point : points)
      {
        m_points.emplace_back(point);
      }
    }

    [[nodiscard]] std::optional<ConvexHull> build()
    {
      if (m_points.size() < 4 || !computeEpsilon())
      {
        return std::nullopt;
      }

      std::array<uint32_t, 4> simplex{};
      if (!findInitialSimplex(simplex))
      {
        return std::nullopt;
      }

      createInitialFaces(simplex);

      if (!processPending())
      {
        return std::nullopt;
      }

      return extract();
    }

  private:
    std::vector<glm::dvec3> m_points;
    double m_eps = 0.0;
    double m_visibilityNoise = 0.0;
    std::vector<HullFace> m_faces;
    std::unordered_map<uint64_t, size_t> m_edgeToFace;
    std::vector<size_t> m_pending;
    std::vector<uint32_t> m_visitStamp;
    uint32_t m_stamp = 0;
    size_t m_aliveFaces = 0;
    size_t m_expansions = 0;
    size_t m_maxExpansions = 0;

    // False when the build was abandoned for growing past what any accepted hull needs.
    [[nodiscard]] bool processPending()
    {
      while (!m_pending.empty())
      {
        const size_t faceIndex = m_pending.back();
        m_pending.pop_back();

        if (m_faces[faceIndex].alive && !m_faces[faceIndex].outside.empty())
        {
          if (m_aliveFaces > maxAliveFaces || m_expansions > m_maxExpansions)
          {
            return false;
          }

          expand(faceIndex);
        }
      }

      return true;
    }

    [[nodiscard]] bool computeEpsilon()
    {
      glm::dvec3 low = m_points.front();
      glm::dvec3 high = m_points.front();

      for (const auto& point : m_points)
      {
        low = glm::min(low, point);
        high = glm::max(high, point);
      }

      const glm::dvec3 size = high - low;
      const double extent = std::max({ size.x, size.y, size.z });

      m_eps = extent * epsilonScale;
      m_visibilityNoise = m_eps * visibilityNoiseFactor;

      return extent > 0.0;
    }

    [[nodiscard]] std::array<uint32_t, 6> findExtremes() const
    {
      std::array<uint32_t, 6> extremes{};

      for (uint32_t i = 1; i < m_points.size(); ++i)
      {
        for (int axis = 0; axis < 3; ++axis)
        {
          if (m_points[i][axis] < m_points[extremes[axis * 2]][axis])
          {
            extremes[axis * 2] = i;
          }

          if (m_points[i][axis] > m_points[extremes[axis * 2 + 1]][axis])
          {
            extremes[axis * 2 + 1] = i;
          }
        }
      }

      return extremes;
    }

    [[nodiscard]] bool findInitialSimplex(std::array<uint32_t, 4>& simplex) const
    {
      const auto extremes = findExtremes();

      double bestDistance = -1.0;
      for (size_t i = 0; i < extremes.size(); ++i)
      {
        for (size_t j = i + 1; j < extremes.size(); ++j)
        {
          const double distance = glm::distance(m_points[extremes[i]], m_points[extremes[j]]);

          if (distance > bestDistance)
          {
            bestDistance = distance;
            simplex[0] = extremes[i];
            simplex[1] = extremes[j];
          }
        }
      }

      if (bestDistance <= m_eps)
      {
        return false;
      }

      return findThirdPoint(simplex) && findFourthPoint(simplex);
    }

    [[nodiscard]] bool findThirdPoint(std::array<uint32_t, 4>& simplex) const
    {
      const glm::dvec3 origin = m_points[simplex[0]];
      const glm::dvec3 direction = glm::normalize(m_points[simplex[1]] - origin);

      double bestDistance = -1.0;
      for (uint32_t i = 0; i < m_points.size(); ++i)
      {
        const double distance = glm::length(glm::cross(m_points[i] - origin, direction));

        if (distance > bestDistance)
        {
          bestDistance = distance;
          simplex[2] = i;
        }
      }

      return bestDistance > m_eps;
    }

    [[nodiscard]] bool findFourthPoint(std::array<uint32_t, 4>& simplex) const
    {
      const glm::dvec3 origin = m_points[simplex[0]];
      const glm::dvec3 normal = glm::normalize(
        glm::cross(m_points[simplex[1]] - origin, m_points[simplex[2]] - origin));

      double bestDistance = -1.0;
      for (uint32_t i = 0; i < m_points.size(); ++i)
      {
        const double distance = std::abs(glm::dot(normal, m_points[i] - origin));

        if (distance > bestDistance)
        {
          bestDistance = distance;
          simplex[3] = i;
        }
      }

      return bestDistance > m_eps;
    }

    [[nodiscard]] static double distanceTo(const HullFace& face, const glm::dvec3& point)
    {
      return glm::dot(face.normal, point) - face.offset;
    }

    // A face too thin to have a normal keeps a zero normal, so no point is ever above it.
    [[nodiscard]] HullFace makeFace(const uint32_t a, const uint32_t b, const uint32_t c) const
    {
      HullFace face;
      face.v = { a, b, c };

      const glm::dvec3 cross = glm::cross(m_points[b] - m_points[a], m_points[c] - m_points[a]);
      const double length = glm::length(cross);

      if (length > tinyNormalLength * m_eps * m_eps)
      {
        face.normal = cross / length;
        face.offset = glm::dot(face.normal, m_points[a]);
      }

      return face;
    }

    [[nodiscard]] size_t pushFace(HullFace face)
    {
      const size_t index = m_faces.size();

      for (int edge = 0; edge < 3; ++edge)
      {
        m_edgeToFace[edgeKey(face.v[edge], face.v[(edge + 1) % 3])] = index;
      }

      m_faces.push_back(std::move(face));
      m_visitStamp.push_back(0);
      ++m_aliveFaces;

      return index;
    }

    // Wound so the normal points away from interior, which must lie inside the tetrahedron.
    [[nodiscard]] size_t addInitialFace(const uint32_t a, const uint32_t b, const uint32_t c,
                                        const glm::dvec3& interior)
    {
      HullFace face = makeFace(a, b, c);

      if (distanceTo(face, interior) > 0.0)
      {
        face = makeFace(a, c, b);
      }

      return pushFace(std::move(face));
    }

    void createInitialFaces(const std::array<uint32_t, 4>& simplex)
    {
      const glm::dvec3 centroid = (m_points[simplex[0]] + m_points[simplex[1]] + m_points[simplex[2]]
        + m_points[simplex[3]]) * 0.25;

      std::vector<size_t> created;
      created.push_back(addInitialFace(simplex[0], simplex[1], simplex[2], centroid));
      created.push_back(addInitialFace(simplex[0], simplex[1], simplex[3], centroid));
      created.push_back(addInitialFace(simplex[0], simplex[2], simplex[3], centroid));
      created.push_back(addInitialFace(simplex[1], simplex[2], simplex[3], centroid));

      for (uint32_t i = 0; i < m_points.size(); ++i)
      {
        if (std::ranges::find(simplex, i) == simplex.end())
        {
          assignToFace(i, created);
        }
      }

      queueFaces(created);
    }

    // Hands the point to the first candidate face it lies outside of; false when it is inside them all.
    bool assignToFace(const uint32_t point, const std::vector<size_t>& candidates)
    {
      for (const size_t faceIndex : candidates)
      {
        if (distanceTo(m_faces[faceIndex], m_points[point]) > m_eps)
        {
          m_faces[faceIndex].outside.push_back(point);
          return true;
        }
      }

      return false;
    }

    void queueFaces(const std::vector<size_t>& faces)
    {
      for (const size_t faceIndex : faces)
      {
        if (!m_faces[faceIndex].outside.empty())
        {
          m_pending.push_back(faceIndex);
        }
      }
    }

    [[nodiscard]] uint32_t furthestOutside(const HullFace& face) const
    {
      uint32_t furthest = face.outside.front();
      double furthestDistance = distanceTo(face, m_points[furthest]);

      for (const uint32_t point : face.outside)
      {
        if (const double distance = distanceTo(face, m_points[point]); distance > furthestDistance)
        {
          furthestDistance = distance;
          furthest = point;
        }
      }

      return furthest;
    }

    // Breadth-first from the seed over the faces the apex can see, so the cost follows the visible patch
    // and not the whole hull.
    [[nodiscard]] std::vector<size_t> collectVisible(const size_t seed, const glm::dvec3& apex)
    {
      std::vector<size_t> visible{ seed };
      m_visitStamp[seed] = m_stamp;

      for (size_t next = 0; next < visible.size(); ++next)
      {
        const auto& face = m_faces[visible[next]];

        for (int edge = 0; edge < 3; ++edge)
        {
          const auto neighbor = m_edgeToFace.find(edgeKey(face.v[(edge + 1) % 3], face.v[edge]));

          if (neighbor == m_edgeToFace.end() || isVisible(neighbor->second))
          {
            continue;
          }

          // A face the apex is merely within epsilon of must be replaced too, or the cone built on the
          // horizon folds back over it and the hull stops being convex.
          if (distanceTo(m_faces[neighbor->second], apex) > -m_visibilityNoise)
          {
            m_visitStamp[neighbor->second] = m_stamp;
            visible.push_back(neighbor->second);
          }
        }
      }

      return visible;
    }

    [[nodiscard]] bool isVisible(const size_t faceIndex) const
    {
      return m_visitStamp[faceIndex] == m_stamp;
    }

    [[nodiscard]] std::vector<HullEdge> findHorizon(const std::vector<size_t>& visible) const
    {
      std::vector<HullEdge> horizon;

      for (const size_t faceIndex : visible)
      {
        const auto& face = m_faces[faceIndex];

        for (int edge = 0; edge < 3; ++edge)
        {
          const uint32_t from = face.v[edge];
          const uint32_t to = face.v[(edge + 1) % 3];
          const auto neighbor = m_edgeToFace.find(edgeKey(to, from));

          if (neighbor == m_edgeToFace.end() || !isVisible(neighbor->second))
          {
            horizon.emplace_back(from, to);
          }
        }
      }

      return horizon;
    }

    // Removes the visible faces and returns the points they held, apex excluded.
    [[nodiscard]] std::vector<uint32_t> removeFaces(const std::vector<size_t>& visible, const uint32_t apexIndex)
    {
      std::vector<uint32_t> orphans;

      for (const size_t faceIndex : visible)
      {
        auto& face = m_faces[faceIndex];

        for (const uint32_t point : face.outside)
        {
          if (point != apexIndex)
          {
            orphans.push_back(point);
          }
        }

        for (int edge = 0; edge < 3; ++edge)
        {
          m_edgeToFace.erase(edgeKey(face.v[edge], face.v[(edge + 1) % 3]));
        }

        face.alive = false;
        --m_aliveFaces;
        face.outside.clear();
        face.outside.shrink_to_fit();
      }

      return orphans;
    }

    void expand(const size_t seedFace)
    {
      const uint32_t apexIndex = furthestOutside(m_faces[seedFace]);
      const glm::dvec3 apex = m_points[apexIndex];

      ++m_expansions;
      ++m_stamp;
      const auto visible = collectVisible(seedFace, apex);
      const auto horizon = findHorizon(visible);
      const auto orphans = removeFaces(visible, apexIndex);

      // (from, to, apex) keeps the edge direction of the visible face it replaces, which is the winding
      // the surviving neighbor across that edge expects.
      std::vector<size_t> created;
      created.reserve(horizon.size());

      for (const auto& [from, to] : horizon)
      {
        created.push_back(pushFace(makeFace(from, to, apexIndex)));
      }

      for (const uint32_t point : orphans)
      {
        assignToFace(point, created);
      }

      queueFaces(created);
    }

    [[nodiscard]] std::optional<ConvexHull> extract() const
    {
      ConvexHull hull;
      std::unordered_map<uint32_t, uint32_t> remap;

      for (const auto& face : m_faces)
      {
        if (!face.alive)
        {
          continue;
        }

        for (const uint32_t original : face.v)
        {
          const auto [slot, inserted] = remap.try_emplace(original, static_cast<uint32_t>(hull.vertices.size()));

          if (inserted)
          {
            hull.vertices.emplace_back(m_points[original]);
          }

          hull.indices.push_back(slot->second);
        }
      }

      if (hull.vertices.size() < 4)
      {
        return std::nullopt;
      }

      return hull;
    }
  };
}

std::optional<ConvexHull> quickHull(const std::span<const glm::vec3> points)
{
  return QuickHullBuilder(points).build();
}
