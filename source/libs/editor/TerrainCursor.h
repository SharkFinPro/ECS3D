#ifndef TERRAINCURSOR_H
#define TERRAINCURSOR_H

#include "Gizmo.h"
#include <terrain/TerrainRaycast.h>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

// A terrain object's place in the world. Terrain-local space has no scale, matching the engine's stream focus
// (TerrainStreamFocus) and the model matrix the renderer draws the surface with.
struct TerrainFrame
{
  glm::vec3 position{ 0.0f };
  glm::quat orientation{ 1.0f, 0.0f, 0.0f, 0.0f };

  [[nodiscard]] glm::vec3 pointToLocal(const glm::vec3& world) const
  {
    return glm::inverse(orientation) * (world - position);
  }

  [[nodiscard]] glm::vec3 pointToWorld(const glm::vec3& local) const
  {
    return orientation * local + position;
  }

  [[nodiscard]] glm::vec3 directionToLocal(const glm::vec3& world) const
  {
    return glm::inverse(orientation) * world;
  }

  [[nodiscard]] glm::vec3 directionToWorld(const glm::vec3& local) const
  {
    return orientation * local;
  }
};

[[nodiscard]] inline gizmo::Ray rayToLocal(const TerrainFrame& frame, const gizmo::Ray& world)
{
  return { frame.pointToLocal(world.origin), frame.directionToLocal(world.direction) };
}

[[nodiscard]] inline terrain::RayHit hitToWorld(const TerrainFrame& frame, const terrain::RayHit& local)
{
  terrain::RayHit hit = local;
  hit.point = frame.pointToWorld(local.point);
  hit.normal = frame.directionToWorld(local.normal);
  return hit;
}

inline constexpr size_t brushRingSegments = 48;

// segments points of a circle of the given radius around center, in the plane perpendicular to normal. A zero or
// non-finite normal falls back to +Y.
[[nodiscard]] inline std::vector<glm::vec3> brushRing(const glm::vec3& center, const glm::vec3& normal,
                                                      const float radius, const size_t segments = brushRingSegments)
{
  glm::vec3 axis{ 0.0f, 1.0f, 0.0f };
  const float length = glm::length(normal);

  if (std::isfinite(length) && length > 1.0e-6f)
  {
    axis = normal / length;
  }

  const glm::vec3 reference = std::abs(axis.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
  const glm::vec3 u = glm::normalize(glm::cross(axis, reference));
  const glm::vec3 v = glm::cross(axis, u);

  std::vector<glm::vec3> points;
  points.reserve(segments);

  for (size_t index = 0; index < segments; ++index)
  {
    const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(index) / static_cast<float>(segments);
    points.push_back(center + radius * (std::cos(angle) * u + std::sin(angle) * v));
  }

  return points;
}

#endif //TERRAINCURSOR_H
