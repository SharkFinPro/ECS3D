#ifndef SCRIPTSYSTEM_H
#define SCRIPTSYSTEM_H

#include <nlohmann/json_fwd.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <uuid.h>

class ManagedHost;
class ScriptEngine;
class ObjectManager;
class Object;
class Script;

// Which contact-lifecycle callback a dispatched collision event maps to. The integer values are the
// wire contract with ScriptBridge.Bridge.onCollision (0 = enter, 1 = stay, 2 = exit) - keep them in sync.
enum class CollisionEvent {
  enter = 0,
  stay = 1,
  exit = 2
};

// Drives the live C# script instances on the server. Owns the ScriptEngine (the ScriptBridge ABI)
// and tracks which (uuid, class) pairs are attached, the exposed-field cache, and the hot-reload
// file snapshot. Each Script data component carries only a field blob; ScriptSystem syncs that blob
// <-> the live instance (writes it on attach/start, reads it back before a snapshot). Server only.
class ScriptSystem {
public:
  explicit ScriptSystem(std::shared_ptr<ManagedHost> host);

  ~ScriptSystem();

  // Attach + start every Script's managed instance, pushing each Script's saved field blob into it.
  void start(ObjectManager& objectManager);

  // Stop + detach every managed instance.
  void stop(ObjectManager& objectManager);

  void fixedUpdate(ObjectManager& objectManager, float dt);

  // Drives each script's variableUpdate (where gameplay reads input). The server has no render frame,
  // so it runs this once per tick from the networked InputState; it must run before fixedUpdate so any
  // force the script queues from input is applied the same tick.
  void variableUpdate(ObjectManager& objectManager) const;

  // Deliver one collision event to both objects' scripts: objectA's scripts learn of objectB and vice
  // versa. Fed plain uuid pairs by the app (from CollisionSystem's enter/stay/exit lists) so scripting
  // stays independent of sim. Safe if an object was destroyed (its side is skipped); the survivor still
  // gets the event.
  void dispatchCollisionEvent(ObjectManager& objectManager,
                              const uuids::uuid& objectA,
                              const uuids::uuid& objectB,
                              CollisionEvent event) const;

  // Attach any scripts that haven't been attached yet (without running their lifecycle methods).
  // Call this before syncFieldsToData so newly added scripts have a field-cache entry.
  void attachAll(ObjectManager& objectManager);

  // Refresh every Script's field blob from its live instance so a serialize()/Snapshot carries the
  // current values. Call this before building a Snapshot.
  void syncFieldsToData(const ObjectManager& objectManager) const;

  // Push an updated field blob from the editor into the live C# instance immediately. Call this after
  // applyComponentEdit so the running script sees the new values without waiting for a restart.
  void applyScriptFieldEdit(const uuids::uuid& objectUUID,
                            const std::string& className,
                            const nlohmann::json& fields) const;

private:
  std::shared_ptr<ManagedHost> m_host;
  std::unique_ptr<ScriptEngine> m_engine;

  struct ExposedField {
    std::string name;
    std::string type;
  };

  // key = uuid + "_" + className (cacheKey)
  std::unordered_set<std::string> m_attached;
  std::unordered_map<std::string, std::vector<ExposedField>> m_fieldCache;

  // Hot-reload: poll the user-script directory's write times a couple times a second; on change,
  // snapshot field values back into the data, reload the bridge, and let fixedUpdate re-attach lazily.
  using ScriptsSnapshot = std::unordered_map<std::string, std::filesystem::file_time_type>;
  ScriptsSnapshot m_scriptsSnapshot;
  float m_timeSinceLastSnapshot = 0.0f;

  void ensureEngine();

  void checkForScriptChanges(const ObjectManager& objectManager, float dt);

  // Create the managed instance, cache its exposed fields, and push the Script's saved field blob in.
  void attach(const Object& object, const Script& script);

  void detach(const uuids::uuid& uuid, const std::string& className);

  // Deliver one directed collision event: every attached script on `target` learns it is touching
  // `other`. One half of dispatchCollisionEvent's both-directions delivery.
  void dispatchCollisionTo(ObjectManager& objectManager,
                           const uuids::uuid& target,
                           const uuids::uuid& other,
                           CollisionEvent event) const;

  void writeFieldsToInstance(const uuids::uuid& uuid,
                             const std::string& className,
                             const nlohmann::json& fields) const;

  [[nodiscard]] nlohmann::json readFieldsFromInstance(const uuids::uuid& uuid,
                                                      const std::string& className) const;

  [[nodiscard]] bool isAttached(const uuids::uuid& uuid, const std::string& className) const;

  [[nodiscard]] static std::string cacheKey(const uuids::uuid& uuid, const std::string& className);

  [[nodiscard]] static ScriptsSnapshot takeSnapshot();
};



#endif //SCRIPTSYSTEM_H
