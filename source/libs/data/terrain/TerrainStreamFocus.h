#ifndef TERRAINSTREAMFOCUS_H
#define TERRAINSTREAMFOCUS_H

#include <glm/vec3.hpp>
#include <cstdint>
#include <optional>
#include <uuid.h>

class ObjectManager;

namespace terrain
{
  // The point a joiner's stream starts from, in the terrain's sample space: worldPoint brought into the local frame of
  // the first object in the scene whose Terrain component names the terrain (position and rotation, never scale, as
  // the collider places it), divided by the voxel size. With no such object the world point itself is used. "First"
  // is the lowest object uuid among the matches.
  [[nodiscard]] glm::vec3 streamFocus(const ObjectManager& objects, const uuids::uuid& terrain, glm::vec3 worldPoint,
                                      float voxelSize);

  // The world position of the object whose PlayerController has this slot (the lowest uuid if several), or nullopt.
  [[nodiscard]] std::optional<glm::vec3> playerPosition(const ObjectManager& objects, int32_t slot);
}

#endif //TERRAINSTREAMFOCUS_H
