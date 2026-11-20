#ifndef TERRAINCONTACTINTERNAL_H
#define TERRAINCONTACTINTERNAL_H

#include "NarrowPhase.h"
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

class Collider;
class Terrain;

namespace terrain {
  class Sampler;
  class TerrainVolume;
}

// What the sphere and capsule path in TerrainContact.cpp and the polytope path in TerrainPolytopeContact.cpp share;
// not part of the terrain contact interface.
namespace collisions::detail {
  struct TerrainFrame {
    glm::vec3 origin;
    glm::quat rotation;
    glm::quat inverse;

    [[nodiscard]] glm::vec3 toLocal(const glm::vec3& world) const
    {
      return inverse * (world - origin);
    }

    [[nodiscard]] glm::vec3 pointToWorld(const glm::vec3& local) const
    {
      return origin + rotation * local;
    }
  };

  struct TerrainGround {
    std::shared_ptr<const terrain::TerrainVolume> volume;
    TerrainFrame frame;
  };

  // Terrain-local.
  struct TerrainSurface {
    glm::vec3 point;
    glm::vec3 normal;
    float gradientLength;
  };

  // World space; distance is how far the collider's surface is from the ground along normal (negative inside).
  struct TerrainCandidate {
    glm::vec3 point;
    glm::vec3 normal;
    float distance;
  };

  using TerrainCluster = std::vector<TerrainCandidate>;

  // The terrain's volume and placement, or nothing when its collision is off or it has no volume or transform.
  [[nodiscard]] std::optional<TerrainGround> prepareGround(Terrain& terrain);

  // Whether any chunk holding ground lies within reach of the world-space box from low to high.
  [[nodiscard]] bool reachesGround(const TerrainGround& ground, const glm::vec3& low, const glm::vec3& high,
                                   float reach);

  [[nodiscard]] std::optional<TerrainSurface> findSurface(terrain::Sampler& sampler, const glm::vec3& from);

  // centerDistance is the raw field distance at center, in local units.
  [[nodiscard]] TerrainCandidate measureCandidate(const TerrainFrame& frame, const TerrainSurface& surface,
                                                  const glm::vec3& center, float centerDistance, float radius);

  [[nodiscard]] bool reachesAlong(const TerrainCandidate& candidate, const glm::vec3& sweep);

  [[nodiscard]] std::vector<TerrainCluster> clusterByWall(std::vector<TerrainCandidate> candidates);

  // The cluster's deepest point, or up to maxPoints of them spread apart.
  [[nodiscard]] Contact makeTerrainContact(const TerrainCluster& cluster, size_t maxPoints, const glm::vec3& sweep);

  inline constexpr size_t maxTerrainQueryPoints = 512;

  [[nodiscard]] bool isPolytopeCollider(const Collider& collider);

  [[nodiscard]] std::vector<Contact> findPolytopeTerrainContacts(Collider& collider, Terrain& terrain,
                                                                 const glm::vec3& sweep);

  [[nodiscard]] bool polytopeIntersectsTerrain(Collider& collider, Terrain& terrain);

  // The world-space points a box or convex hull is tested at: every vertex, then points along its edges and over its
  // faces no more than a voxel apart, thinned to maxTerrainQueryPoints except for the vertices.
  [[nodiscard]] std::vector<glm::vec3> polytopeQueryPoints(Collider& collider, const TerrainGround& ground,
                                                           terrain::Sampler& sampler);
}

#endif //TERRAINCONTACTINTERNAL_H
