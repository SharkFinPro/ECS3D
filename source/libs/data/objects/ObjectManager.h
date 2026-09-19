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

  // Same as addObjectToRoot, but at a specific sibling index (clamped to m_objects.size(), so an index
  // past the end appends) instead of always at the end - restoreSubtree uses this to put a restored root
  // back exactly where it was.
  void addObjectToRoot(const std::shared_ptr<Object>& object, std::size_t index);

  void removeObjectFromRoot(const std::shared_ptr<Object>& object);

  // rootUUID, when given, is the uuid the copy's root takes instead of a generated one; its descendants
  // are still given fresh ones. Passed through to instantiateUnder - see there.
  void duplicateObject(const std::shared_ptr<Object>& object, const uuids::uuid* rootUUID = nullptr);

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
  //
  // rootUUID, when given, is the uuid the new root takes instead of the generated one; the descendants
  // still get fresh uuids. The caller is the one claiming that uuid is free - nothing here checks the
  // scene for it.
  std::shared_ptr<Object> instantiateUnder(const nlohmann::json& objectData,
                                           const std::shared_ptr<Object>& parent,
                                           const uuids::uuid* rootUUID = nullptr);

  // RAII guard a caller holds around a pass that ranges over getAllObjects() and runs arbitrary user code
  // (script callbacks) inside the loop - ScriptSystem::fixedUpdate/variableUpdate. While any guard is
  // alive, addObject defers the m_allObjects/m_objects append that would otherwise reallocate the vector
  // out from under that range-for; everything else addObject does (parenting, starting the object) still
  // happens immediately, so the spawning script can use the object right away. Depth-counted so a nested
  // guard is harmless, but the flush itself is a separate, explicit call (flushPendingAdditions) rather
  // than something the guard's destructor triggers - see there for why.
  class ScriptPassGuard {
  public:
    explicit ScriptPassGuard(ObjectManager& manager);
    ~ScriptPassGuard();

    ScriptPassGuard(const ScriptPassGuard&) = delete;
    ScriptPassGuard& operator=(const ScriptPassGuard&) = delete;

  private:
    ObjectManager& m_manager;
  };

  // Moves every addition queued while a ScriptPassGuard was alive into m_allObjects/m_objects. Called
  // once per server frame, at the same point deleteObjectsMarkedForDeletion is (ServerApp::
  // broadcastStructuralChanges) - so an object a script spawns mid-pass does not tick again until the
  // scripts' next pass, exactly like a destroyed object does not truly leave until that same point either.
  void flushPendingAdditions();

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

  // Rebuild a subtree from an inline serialized body under parent (null = scene root) at sibling index,
  // with every uuid the body carries PRESERVED rather than reassigned - unlike instantiate/instantiateUnder,
  // which mint fresh ones for a prefab drop or a duplicate. This is the undo-of-a-deletion op: the undo
  // history already names the removed subtree's objects by their original uuids (EditCommand's
  // RemoveObjectData), and giving them new ones on restore would break every entry that points at them.
  // Precondition, checked by the caller (applySceneEdit's restoreObject op): no uuid in body already names
  // a live object. Throws (leaving nothing behind - discardSubtree, then rethrow) exactly like
  // instantiateUnder does on a body naming a component this build does not know.
  [[nodiscard]] std::shared_ptr<Object> restoreSubtree(const nlohmann::json& body,
                                                       const std::shared_ptr<Object>& parent,
                                                       std::size_t index);

  // Deletes object and its whole subtree immediately, promoting nothing - the opposite of removeObject,
  // which defers to deleteObjectsMarkedForDeletion and promotes the removed object's children up to its
  // own parent. This is an edit-time structural op (applySceneEdit's removeSubtree): it only ever runs on
  // a stopped scene, and the server re-snapshots right after, so there is no live tick to protect and no
  // reason to keep the subtree's now-orphaned children in the scene at all.
  void removeSubtree(const std::shared_ptr<Object>& object);

  [[nodiscard]] std::shared_ptr<Object> getObjectByUUID(uuids::uuid uuid) const;

  [[nodiscard]] const std::vector<std::shared_ptr<Object>>& getObjects() const;

  [[nodiscard]] const std::vector<std::shared_ptr<Object>>& getAllObjects() const;

  // Objects addObject deferred while a ScriptPassGuard is alive (see there), not yet spliced into
  // getAllObjects()/getObjects() by flushPendingAdditions. getObjectByUUID already checks this list;
  // any other caller that needs a script pass's world to look whole - not just individual uuid lookups -
  // (e.g. a World binding that scans every object) needs to check it too, or it will miss an object a
  // script spawned earlier in the same pass while getObjectByUUID/objectExists/destroyObject on that same
  // object would have found it.
  [[nodiscard]] const std::vector<std::shared_ptr<Object>>& getPendingAdditions() const;

private:
  std::shared_ptr<ComponentRegistry> m_componentRegistry;

  std::vector<std::shared_ptr<Object>> m_objects;

  std::vector<std::shared_ptr<Object>> m_allObjects;

  std::vector<std::shared_ptr<Object>> m_objectsToRemove;

  // Objects addObject deferred while a ScriptPassGuard was alive, waiting on flushPendingAdditions.
  std::vector<std::shared_ptr<Object>> m_pendingAdditions;

  // >0 while at least one ScriptPassGuard is alive - see there. addObject consults it to decide whether
  // to defer.
  int m_scriptPassDepth = 0;

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
