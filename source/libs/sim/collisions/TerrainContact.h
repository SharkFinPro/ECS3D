#ifndef TERRAINCONTACT_H
#define TERRAINCONTACT_H

#include "NarrowPhase.h"
#include <glm/vec3.hpp>
#include <cstddef>
#include <vector>

class Collider;
class Terrain;

namespace collisions {
  inline constexpr size_t maxTerrainManifolds = 4;

  // Contacts of collider against terrain, each from collider's point of view (the normal pushes it out of the
  // ground). sweep is collider's displacement over the coming tick (zero for "as it stands"). Empty when apart.
  // A sweep longer than a voxel is sphere traced to where the collider first nears the ground, and a query point
  // deeper than the distance band is stepped back out of the ground against the sweep (up when there is none).
  // Spheres, capsules, boxes and convex hulls are handled; any other collider, a terrain without a volume and a
  // terrain with collision disabled give nothing. A sphere or capsule radius must stay below the distance band (about
  // 3.5 voxels): past it the center reads saturated with no gradient and the contact is silently lost. A box or hull
  // reports up to four points on its own surface per contact.
  //
  // Reads the collider's world shape (CapsuleCollider::worldShape through getSegment and getRadius, a sphere's
  // transform, a box's world frame, a hull's world vertices and faces), which caches lazily, so a caller on several
  // threads must warm the collider first (collisions::warmContactCaches for a box or hull); the terrain side only
  // reads.
  [[nodiscard]] std::vector<Contact> findTerrainContacts(Collider& collider, Terrain& terrain, const glm::vec3& sweep);

  // Whether any query point is inside the ground now (distance < 0).
  [[nodiscard]] bool intersectsTerrain(Collider& collider, Terrain& terrain);

  // Whether collider, carried along sweep, would reach the ground: findTerrainContacts would report a contact.
  [[nodiscard]] bool approachesTerrain(Collider& collider, Terrain& terrain, const glm::vec3& sweep);
}

#endif //TERRAINCONTACT_H
