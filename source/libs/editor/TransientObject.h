#ifndef TRANSIENTOBJECT_H
#define TRANSIENTOBJECT_H

#include <memory>
#include <optional>
#include <string>

class ComponentRegistry;
class Object;
class ObjectManager;

// A detached Object deserialized from a serialized-object blob (a prefab's AssetRecord::body), living in a
// private scratch ObjectManager that is never wired to a scene, the SceneManager, or replication. It exists
// solely so the editor's component editors can operate on prefab contents out-of-scene (see ROADMAP Phase 4
// / B2). uuids are preserved on load (not reassigned like instantiation), so the body stays stable across
// edits; re-serialize after an edit to produce the updated body to send.
//
// The private manager is what lets the whole existing deserialize path (Object + children) and
// replication::applySceneEdit be reused verbatim — the "detached" object is detached from any *scene*, not
// from an ObjectManager, which Object fundamentally needs to deserialize and to apply structural edits.
class TransientObject {
public:
  explicit TransientObject(std::shared_ptr<ComponentRegistry> componentRegistry);

  ~TransientObject();

  // (Re)build the detached object from `body` when it differs from the blob currently loaded — an external
  // change (a fresh snapshot, or a different prefab selected). Returns true when a rebuild happened (any
  // pointers the caller cached into the old object are now stale). A malformed/empty body clears the object
  // (object() becomes null) and is remembered so it isn't re-parsed every frame. Safe to call every frame.
  bool syncFromBody(const std::string& body);

  // Adopt `body` as the currently-loaded blob WITHOUT rebuilding — call after applying a local edit whose
  // serialized body you already hold, so the next syncFromBody(recordBody) with that same string is a no-op
  // instead of a rebuild that would discard the live (edited) object.
  void markSynced(const std::string& body);

  // The scratch manager backing the object — replication::applySceneEdit targets this so structural edits
  // (add/remove component, rename, add script) apply to the detached object in place. Null until a
  // successful sync.
  [[nodiscard]] ObjectManager* manager() const;

  // The detached root object, or null when the last sync had no usable body.
  [[nodiscard]] const std::shared_ptr<Object>& object() const;

  // Serialize the current object back to a body blob (Object::serialize().dump()), or an empty string when
  // there is no object. Feed the result to markSynced after sending it.
  [[nodiscard]] std::string serialize() const;

private:
  void rebuild(const std::string& body);

  std::shared_ptr<ComponentRegistry> m_componentRegistry;

  std::unique_ptr<ObjectManager> m_manager;
  std::shared_ptr<Object> m_object;

  // The blob currently reflected by m_object (whether it built successfully or not), used to skip redundant
  // rebuilds. nullopt until the first sync.
  std::optional<std::string> m_loadedBody;
};

#endif //TRANSIENTOBJECT_H
