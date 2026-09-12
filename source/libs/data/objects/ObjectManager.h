#ifndef OBJECTMANAGER_H
#define OBJECTMANAGER_H

#include <nlohmann/json_fwd.hpp>
#include <cstddef>
#include <memory>
#include <random>
#include <vector>
#include <uuid.h>

namespace net {
  class Message;
  class MessageReader;
}

class Object;
class ComponentRegistry;

class ObjectManager {
public:
  explicit ObjectManager(std::shared_ptr<ComponentRegistry> componentRegistry);

  [[nodiscard]] std::shared_ptr<ComponentRegistry> getComponentRegistry() const;

  [[nodiscard]] uuids::uuid createUUID();

  // Registers the object, wiring it to this manager and to its parent. Starts it too while the scene is
  // running, so an object added mid-run is live rather than inert - a stopped component reads and writes
  // its authored value, which is what the scene saves. duplicateObject and instantiate inherit that.
  void addObject(const std::shared_ptr<Object>& object);

  void addObjectToRoot(const std::shared_ptr<Object>& object);

  void removeObjectFromRoot(const std::shared_ptr<Object>& object);

  void duplicateObject(const std::shared_ptr<Object>& object);

  // Build a live object (and its whole subtree) from a serialized-object blob, giving every node a fresh
  // uuid, and add it at the scene root. This is the prefab instantiation path (a prefab's body is one
  // Object::serialize() blob - see AssetRegistry::getPrefabBody) and the shared core of duplicateObject.
  //
  // Throws if objectData isn't a well-formed serialized object (unknown component type, missing uuid/name);
  // callers on the tick loop must guard. Every node is registered through addObject, so the subtree comes
  // back started when the manager is running.
  std::shared_ptr<Object> instantiate(const nlohmann::json& objectData);

  // Same as instantiate, but rooted under parent (null = scene root) instead of always at the root - the
  // editor's instantiatePrefab op uses this when the prefab was dropped onto an existing object.
  std::shared_ptr<Object> instantiateUnder(const nlohmann::json& objectData,
                                           const std::shared_ptr<Object>& parent);

  void start();

  void stop();

  // Rebuild the manager's objects from a save-shaped objects array (the "objects" field ObjectManager::
  // serialize() writes), replacing whatever it currently holds and preserving each object's uuid - unlike
  // instantiate, which assigns fresh ones. Used to put a scene back to its authored structure after a run
  // (spawn/destroy/reparent undone), so editor selections and cross-references to authored objects survive.
  void restoreFromJSON(const nlohmann::json& objectsData);

  [[nodiscard]] nlohmann::json serialize() const;

  void pack(net::Message& message) const;

  void unpack(net::MessageReader& messageReader);

  // Queues object for the next deleteObjectsMarkedForDeletion pass. Idempotent within a tick: marking an
  // object already queued is a no-op, so two paths reacting to the same event in one tick still produce
  // exactly one deletion. Returns true when this call newly marked the object, false when it was already
  // marked - callers that broadcast a destroy should do so only on true. Not [[nodiscard]]: most callers
  // (a structural scene edit, a client applying a destroy it was already told about) legitimately ignore
  // the result.
  bool removeObject(const std::shared_ptr<Object>& object);

  // Drop a subtree that was never fully built. Not the deletion lifecycle: it defers nothing and does not
  // reparent children, it just unregisters what was registered. For a subtree that is already live in the
  // scene, removeObject is the one you want. Taken by value because it erases from vectors it may alias.
  void discardSubtree(std::shared_ptr<Object> root);

  void deleteObjectsMarkedForDeletion();

  [[nodiscard]] std::shared_ptr<Object> getObjectByUUID(uuids::uuid uuid) const;

  [[nodiscard]] const std::vector<std::shared_ptr<Object>>& getObjects() const;

  [[nodiscard]] const std::vector<std::shared_ptr<Object>>& getAllObjects() const;

private:
  std::shared_ptr<ComponentRegistry> m_componentRegistry;

  std::vector<std::shared_ptr<Object>> m_objects;

  std::vector<std::shared_ptr<Object>> m_allObjects;

  std::vector<std::shared_ptr<Object>> m_objectsToRemove;

  // Whether the scene is running, set by start and cleared by stop. addObject consults it - see there.
  bool m_started = false;

  std::mt19937 m_rng;
  uuids::uuid_random_generator m_uuidGenerator;

  void eraseSubtree(const std::shared_ptr<Object>& object);

  // Recursively replace the serialized object's (and its children's) uuids with fresh ones. Throws past
  // maxObjectDepth (Object.h) rather than recursing further into an attacker-sized prefab/duplicate body.
  void reassignUUIDs(nlohmann::json& objectData, std::size_t depth = 0);
};



#endif //OBJECTMANAGER_H
