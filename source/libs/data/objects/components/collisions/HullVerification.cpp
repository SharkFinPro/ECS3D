#include "HullVerification.h"
#include <cstdint>
#include <unordered_set>

namespace hull {
namespace {
  // Every input point must lie within this many build tolerances of every face plane.
  constexpr double verifyEpsilonFactor = 10.0;

  // The alive faces that have a normal.
  [[nodiscard]] std::vector<size_t> liveFaces(const std::vector<HullFace>& faces)
  {
    std::vector<size_t> live;

    for (size_t i = 0; i < faces.size(); ++i)
    {
      if (faces[i].alive && glm::dot(faces[i].normal, faces[i].normal) > 0.0)
      {
        live.push_back(i);
      }
    }

    return live;
  }

  // Every directed edge appears once and has its reverse, and V - E + F == 2.
  [[nodiscard]] bool isClosed(const std::vector<HullFace>& faces, const std::vector<size_t>& live)
  {
    std::unordered_set<uint64_t> edges;
    std::unordered_set<uint32_t> vertices;

    for (const size_t faceIndex : live)
    {
      const auto& face = faces[faceIndex];

      for (int edge = 0; edge < 3; ++edge)
      {
        edges.insert(edgeKey(face.v[edge], face.v[(edge + 1) % 3]));
        vertices.insert(face.v[edge]);
      }
    }

    if (edges.size() != live.size() * 3)
    {
      return false;
    }

    for (const uint64_t key : edges)
    {
      if (!edges.contains(edgeKey(static_cast<uint32_t>(key), static_cast<uint32_t>(key >> 32))))
      {
        return false;
      }
    }

    return vertices.size() + live.size() == edges.size() / 2 + 2;
  }
}

std::vector<size_t> assignMissedPoints(std::vector<HullFace>& faces, const std::vector<glm::dvec3>& points,
                                       const double eps)
{
  const auto live = liveFaces(faces);

  std::vector<char> isVertex(points.size(), 0);
  for (const size_t faceIndex : live)
  {
    for (const uint32_t vertex : faces[faceIndex].v)
    {
      isVertex[vertex] = 1;
    }
  }

  bool assigned = false;
  for (uint32_t point = 0; point < points.size(); ++point)
  {
    if (isVertex[point])
    {
      continue;
    }

    for (const size_t faceIndex : live)
    {
      if (distanceTo(faces[faceIndex], points[point]) > eps)
      {
        faces[faceIndex].outside.push_back(point);
        assigned = true;
        break;
      }
    }
  }

  return assigned ? live : std::vector<size_t>{};
}

bool isVerifiedHull(const std::vector<HullFace>& faces, const size_t aliveFaces,
                    const std::vector<glm::dvec3>& points, const double eps)
{
  const auto live = liveFaces(faces);

  if (live.size() != aliveFaces || !isClosed(faces, live))
  {
    return false;
  }

  for (const auto& point : points)
  {
    for (const size_t faceIndex : live)
    {
      if (distanceTo(faces[faceIndex], point) > verifyEpsilonFactor * eps)
      {
        return false;
      }
    }
  }

  return true;
}
}
