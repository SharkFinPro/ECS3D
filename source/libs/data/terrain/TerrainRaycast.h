#ifndef TERRAINRAYCAST_H
#define TERRAINRAYCAST_H

#include "TerrainVolume.h"
#include <glm/vec3.hpp>
#include <optional>

namespace terrain
{
  struct RayHit
  {
    glm::vec3 point{ 0.0f };
    glm::vec3 normal{ 0.0f };
    float distance = 0.0f;
  };

  // Terrain-local space. The direction need not be normalized, and the distance is along the normalized direction.
  // A ray that starts inside the ground hits at distance 0. A non-finite input, a zero direction or a negative
  // maxDistance finds nothing, and so does a direction shorter than 1e-12 or an origin beyond 1e9 voxels. A ray
  // crossing on the order of a million voxels of present chunks can report a miss when the iteration cap runs out.
  [[nodiscard]] std::optional<RayHit> raycast(const TerrainVolume& volume, glm::vec3 origin, glm::vec3 direction,
                                              float maxDistance);
}

#endif //TERRAINRAYCAST_H
