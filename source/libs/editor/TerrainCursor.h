#ifndef TERRAINCURSOR_H
#define TERRAINCURSOR_H

#include "Gizmo.h"
#include <terrain/TerrainRaycast.h>
#include <terrain/TerrainVolume.h>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
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

// Where the ray crosses the upright plane through anchor that faces the camera; nothing when it looks straight up or
// down or the crossing is behind it or past maxDistance.
[[nodiscard]] inline std::optional<terrain::RayHit> uprightPlaneHit(const glm::vec3& anchor, const gizmo::Ray& ray,
                                                                    const float maxDistance)
{
  const float length = glm::length(ray.direction);
  glm::vec3 facing(ray.direction.x, 0.0f, ray.direction.z);
  const float across = glm::length(facing);

  if (!(length > 1.0e-12f) || !(across > 1.0e-6f * length))
  {
    return std::nullopt;
  }

  const glm::vec3 direction = ray.direction / length;
  facing /= across;

  const float approach = glm::dot(direction, facing);
  const float distance = glm::dot(anchor - ray.origin, facing) / approach;

  if (!std::isfinite(distance) || distance < 0.0f || distance > maxDistance)
  {
    return std::nullopt;
  }

  return terrain::RayHit{ ray.origin + direction * distance, -facing, distance };
}

// The cursor's hit during a stroke, in terrain-local space. It aims first at surface, the terrain as it was when the
// stroke began, so the brush cannot climb the material it just added toward the camera. When that surface has nothing
// within maxJump of anchor (the stroke's last point), the hit is whichever of these lies nearer anchor:
//  - live, the terrain as it is now, unless its hit is well in front of anchor along the ray (that would climb), so a
//    stroke carried over material it drew lands on that material rather than on the terrain behind it;
//  - the upright plane through anchor that faces the camera, so dragging up off the ground raises a column and a wall
//    far behind a column being drawn down does not catch it.
// Failing both it is the surface hit, however far, or nothing.
[[nodiscard]] inline std::optional<terrain::RayHit> strokeCursorHit(const terrain::TerrainVolume& surface,
                                                                    const terrain::TerrainVolume& live,
                                                                    const glm::vec3& anchor, const gizmo::Ray& ray,
                                                                    const float maxDistance, const float maxJump)
{
  const std::optional<terrain::RayHit> onSurface = terrain::raycast(surface, ray.origin, ray.direction, maxDistance);

  if (onSurface && glm::length(onSurface->point - anchor) <= maxJump)
  {
    return onSurface;
  }

  std::optional<terrain::RayHit> best;
  float bestGap = 0.0f;

  const auto consider = [&](const std::optional<terrain::RayHit>& candidate)
  {
    if (!candidate)
    {
      return;
    }

    const float gap = glm::length(candidate->point - anchor);

    if (!best || gap < bestGap)
    {
      best = candidate;
      bestGap = gap;
    }
  };

  const float length = glm::length(ray.direction);

  if (length > 1.0e-12f)
  {
    const float anchorDepth = glm::dot(anchor - ray.origin, ray.direction / length);
    const std::optional<terrain::RayHit> onLive = terrain::raycast(live, ray.origin, ray.direction, maxDistance);

    if (onLive && onLive->distance >= anchorDepth - 0.25f * maxJump &&
        glm::length(onLive->point - anchor) <= maxJump)
    {
      consider(onLive);
    }
  }

  consider(uprightPlaneHit(anchor, ray, maxDistance));

  return best ? best : onSurface;
}

// Like terrain::raycast for a brush of radius around the cursor. The cursor's own hit wins whenever there is one, so
// the brush digs where it points, along the view, rather than snapping to a slope at its rim. Only when the cursor
// points past all terrain are rays offset across the brush's disk cast, so terrain anywhere under the ring is still
// found; the nearest of those along the view wins, moved onto the cursor's own ray at its depth and facing the camera,
// so the brush stays centered under the cursor.
[[nodiscard]] inline std::optional<terrain::RayHit> brushDiskHit(const terrain::TerrainVolume& volume,
                                                                 const gizmo::Ray& ray, const float radius,
                                                                 const float maxDistance)
{
  std::optional<terrain::RayHit> nearest = terrain::raycast(volume, ray.origin, ray.direction, maxDistance);
  const float length = glm::length(ray.direction);

  if (nearest || !(radius > 0.0f) || !(length > 1.0e-12f))
  {
    return nearest;
  }

  const glm::vec3 direction = ray.direction / length;
  const glm::vec3 helper = std::abs(direction.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
  const glm::vec3 across = glm::normalize(glm::cross(direction, helper));
  const glm::vec3 up = glm::cross(across, direction);

  constexpr int spokes = 8;
  constexpr float rings[] = { 0.5f, 1.0f };

  for (const float ring : rings)
  {
    for (int spoke = 0; spoke < spokes; ++spoke)
    {
      const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(spoke) / static_cast<float>(spokes);
      const glm::vec3 offset = (across * std::cos(angle) + up * std::sin(angle)) * (radius * ring);
      const std::optional<terrain::RayHit> hit = terrain::raycast(volume, ray.origin + offset, direction, maxDistance);

      if (hit && (!nearest || hit->distance < nearest->distance))
      {
        nearest = hit;
      }
    }
  }

  if (nearest)
  {
    nearest->point = ray.origin + direction * nearest->distance;
    nearest->normal = -direction;
  }

  return nearest;
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
