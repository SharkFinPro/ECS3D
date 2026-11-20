#ifndef TERRAINCONTACTINTERNAL_H
#define TERRAINCONTACTINTERNAL_H

#include "NarrowPhase.h"
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <cstddef>
#include <limits>
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

  // The raw distance, in voxels, at or below which a point reads saturated inside the ground.
  inline constexpr float deepVoxels = 3.9f;

  // The raw field distance to give measureCandidate for a point found by findDeepSurface, whose own reading is
  // saturated and so says nothing about how deep it is.
  inline constexpr float unknownDepth = std::numeric_limits<float>::lowest();

  // The terrain-local direction a point deeper than the band steps along to leave the ground: against sweep, or up
  // when there is none.
  [[nodiscard]] glm::vec3 stepBackDirection(const TerrainFrame& frame, const glm::vec3& sweep);

  // The surface found by stepping from, a point that reads deeper than the band, a voxel at a time along backward
  // until the field has a gradient again, or leaves the ground (a reading at or above zero, which is also what an
  // absent chunk gives), where the zero crossing is bisected. Nothing when rawDistance, the field distance at from in
  // local units, is not that deep, or no surface lies within 64 voxels.
  [[nodiscard]] std::optional<TerrainSurface> findDeepSurface(terrain::Sampler& sampler, const glm::vec3& from,
                                                              const glm::vec3& backward, float voxelSize,
                                                              float rawDistance);

  // How far along sweep, as a fraction, a sphere at center first comes within the contact margin of the ground,
  // found by sphere tracing. Nothing when sweep is no longer than a voxel, or the sphere never gets there. When the
  // step cap runs out first, the fraction travelled so far.
  [[nodiscard]] std::optional<float> reachAlongSweep(terrain::Sampler& sampler, const TerrainFrame& frame,
                                                     const glm::vec3& center, float radius, const glm::vec3& sweep,
                                                     float voxelSize);

  // Drops the members of cluster more than slack further from the ground than its nearest one: with a long sweep
  // behind them they all reach, but only the leading ones are about to touch.
  void trimToNearest(TerrainCluster& cluster, float slack);

  // Re-expresses a contact found with the collider carried fraction of sweep along and asked for with what was left
  // of it, relative to where the collider really is. depth() is unchanged: the gap closed before the contact moves
  // from the overlap into advance, so the solver lets the collider travel to the surface and no further. The
  // points move back by the distance travelled, which keeps their arms about the collider's center.
  void shiftTracedContact(Contact& contact, const glm::vec3& sweep, float fraction);

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
  // offset is where the collider is taken to stand, relative to where it is, for ranking the edges and faces.
  [[nodiscard]] std::vector<glm::vec3> polytopeQueryPoints(Collider& collider, const TerrainGround& ground,
                                                           terrain::Sampler& sampler,
                                                           const glm::vec3& offset = glm::vec3(0));
}

#endif //TERRAINCONTACTINTERNAL_H
