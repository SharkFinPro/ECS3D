#ifndef SCENEQUERIES_H
#define SCENEQUERIES_H

#include <glm/vec3.hpp>
#include <cstdint>
#include <vector>
#include <uuid.h>

class ObjectManager;

// Analytic scene queries (raycast, sphere overlap) over the collider geometry. Lives in sim because it
// is query *behavior* over the data (the data/systems split), but scripting can't link sim — so the
// server app injects these two statics into BindingContext as function pointers at startup. The
// signatures below therefore use only types both sim and scripting can see (data + glm + uuid) and must
// stay matched to BindingContext::RaycastFn / OverlapSphereFn.
class SceneQueries {
public:
  // Cast a ray (origin + normalized direction is computed internally) against every collider whose layer
  // is in layerMask. Returns true on the nearest hit within maxDistance, writing the outputs only then.
  // ignoreObject (nil = none) is skipped, so a caster can exclude its own collider. A ray that starts
  // inside a collider reports it at contact (distance 0).
  static bool raycast(ObjectManager& objectManager,
                      const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
                      uint32_t layerMask, const uuids::uuid& ignoreObject,
                      uuids::uuid& hitObject, glm::vec3& hitPoint, glm::vec3& hitNormal, float& hitDistance);

  // Collect the uuid of every object whose collider overlaps the sphere and whose layer is in layerMask.
  // ignoreObject (nil = none) is skipped.
  static void overlapSphere(ObjectManager& objectManager,
                            const glm::vec3& center, float radius, uint32_t layerMask,
                            const uuids::uuid& ignoreObject,
                            std::vector<uuids::uuid>& results);
};



#endif //SCENEQUERIES_H
