#ifndef BINDINGCONTEXT_H
#define BINDINGCONTEXT_H

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
class BindingContext {
public:
  static void setObjectManager(ObjectManager* objectManager);

  [[nodiscard]] static ObjectManager* getObjectManager();

  static void recordSpawn(const std::shared_ptr<Object>& object);

  static void recordDestroy(const uuids::uuid& objectUUID);

  // Return the objects spawned / uuids destroyed since the last call, clearing the buffers.
  [[nodiscard]] static std::vector<std::shared_ptr<Object>> takeSpawned();

  [[nodiscard]] static std::vector<uuids::uuid> takeDestroyed();

private:
  static ObjectManager* s_objectManager;

  static std::vector<std::shared_ptr<Object>> s_spawned;
  static std::vector<uuids::uuid> s_destroyed;
};



#endif //BINDINGCONTEXT_H
