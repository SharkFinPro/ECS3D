#ifndef BINDINGCONTEXT_H
#define BINDINGCONTEXT_H

#include <glm/vec3.hpp>
#include <cstdint>
#include <memory>
#include <vector>
#include <uuid.h>

class Object;
class ObjectManager;

// ScriptSystem points this at the server's current ObjectManager each tick so the (static, C-ABI)
// bindings can resolve a uuid to its object.
//
// Runtime spawn/destroy from a script can't broadcast directly (the bindings know nothing about the net
// layer), so the spawn/destroy bindings record what happened here; ServerApp drains these after the tick
// and replicates them (objectSpawned/objectDestroyed) — keeping scripting independent of net/protocol.
//
// Scene queries (raycast/overlap) live in sim, which scripting can't link, so the server app injects them
// here as function pointers at startup (see SceneQueries); the World bindings call through them. Signatures
// use only types both sides share (data + glm + uuid) and must match SceneQueries' statics.
class BindingContext {
public:
  using RaycastFn = bool(*)(ObjectManager&, const glm::vec3&, const glm::vec3&, float, uint32_t,
                            const uuids::uuid&, uuids::uuid&, glm::vec3&, glm::vec3&, float&);
  using OverlapSphereFn = void(*)(ObjectManager&, const glm::vec3&, float, uint32_t,
                                  const uuids::uuid&, std::vector<uuids::uuid>&);

  static void setObjectManager(ObjectManager* objectManager);

  [[nodiscard]] static ObjectManager* getObjectManager();

  static void recordSpawn(const std::shared_ptr<Object>& object);

  static void recordDestroy(const uuids::uuid& objectUUID);

  // Return the objects spawned / uuids destroyed since the last call, clearing the buffers.
  [[nodiscard]] static std::vector<std::shared_ptr<Object>> takeSpawned();

  [[nodiscard]] static std::vector<uuids::uuid> takeDestroyed();

  static void setRaycast(RaycastFn raycast);
  [[nodiscard]] static RaycastFn getRaycast();

  static void setOverlapSphere(OverlapSphereFn overlapSphere);
  [[nodiscard]] static OverlapSphereFn getOverlapSphere();

private:
  static ObjectManager* s_objectManager;

  static std::vector<std::shared_ptr<Object>> s_spawned;
  static std::vector<uuids::uuid> s_destroyed;

  static RaycastFn s_raycast;
  static OverlapSphereFn s_overlapSphere;
};



#endif //BINDINGCONTEXT_H
