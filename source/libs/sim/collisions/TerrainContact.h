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
  // Only spheres and capsules are handled; any other collider, a terrain without a volume and a terrain with
  // collision disabled give nothing. The radius must stay below the distance band (about 3.5 voxels): past it the
  // center reads saturated with no gradient and the contact is silently lost.
  //
  // Reads the collider's world shape (CapsuleCollider::worldShape through getSegment and getRadius, a sphere's
  // transform), which caches lazily, so a caller on several threads must warm the collider first; the terrain side
  // only reads.
  [[nodiscard]] std::vector<Contact> findTerrainContacts(Collider& collider, Terrain& terrain, const glm::vec3& sweep);

  // Whether any query point is inside the ground now (distance < 0).
  [[nodiscard]] bool intersectsTerrain(Collider& collider, Terrain& terrain);

  // Whether collider, carried along sweep, would reach the ground: findTerrainContacts would report a contact.
  [[nodiscard]] bool approachesTerrain(Collider& collider, Terrain& terrain, const glm::vec3& sweep);
}

#endif //TERRAINCONTACT_H
