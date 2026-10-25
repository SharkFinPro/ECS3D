#ifndef SCRIPTSYSTEM_H
#define SCRIPTSYSTEM_H

#include "AttachedScripts.h"
#include "ScriptRuntime.h"
#include "UnresolvedScripts.h"
#include <nlohmann/json_fwd.hpp>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <uuid.h>

class ObjectManager;
class Object;
class Component;
class Script;
class SceneAsset;

// Which contact-lifecycle callback a dispatched collision event maps to. The integer values are the
// wire contract with ScriptBridge.Bridge.onCollision (0 = enter, 1 = stay, 2 = exit) - keep them in sync.
enum class CollisionEvent {
  enter = 0,
  stay = 1,
  exit = 2
};

// Which SceneScript virtual a dispatched scene event maps to. The integer values are the wire contract with
// ScriptBridge.Bridge.onSceneEvent (0 = onSceneLoaded, 1 = onSceneUnloading, 2 = onObjectSpawned,
// 3 = onObjectDestroyed, 4 = onPlayerJoined, 5 = onPlayerLeft) - keep them in sync.
enum class SceneEvent {
  loaded = 0,
  unloading = 1,
  objectSpawned = 2,
  objectDestroyed = 3,
  playerJoined = 4,
  playerLeft = 5
};

// Drives the live C# script instances on the server. Owns the ScriptRuntime (the ScriptBridge ABI)
// and tracks which (uuid, class) pairs are attached, the exposed-field cache, and the hot-reload
// file snapshot. Each Script data component carries only a field blob; ScriptSystem syncs that blob
// <-> the live instance (writes it on attach/start, reads it back before a snapshot). Server only.
// Drive one ScriptSystem through either the SceneAsset overloads or the ObjectManager ones, not both: a hot
// reload from the object-only path clears scene-script state without first syncing scene blobs.
class ScriptSystem {
public:
  using RuntimeFactory = std::function<std::unique_ptr<ScriptRuntime>()>;

  // The factory runs once, on first use, and returns an initialized runtime; a throw leaves the system
  // without one so the next call retries. scriptDirectories are the directories polled for hot reload.
  explicit ScriptSystem(RuntimeFactory makeRuntime,
                        std::vector<std::filesystem::path> scriptDirectories = { scripting::defaultUserScriptsDir });

  explicit ScriptSystem(RuntimeFactory makeRuntime, std::filesystem::path scriptDirectory);

  ~ScriptSystem();

  // Builds the runtime now rather than on first use, so the compile does not land on the first join's snapshot.
  void warmUp();

  // The class names the runtime's last successful compile produced; nullopt before the runtime has compiled.
  [[nodiscard]] std::optional<std::vector<ScriptClassInfo>> scriptClassNames() const;

  // Attach + start every Script's managed instance, pushing each Script's saved field blob into it.
  void start(ObjectManager& objectManager);

  // Stop + detach every managed instance.
  void stop(ObjectManager& objectManager);

  void fixedUpdate(ObjectManager& objectManager, float dt);

  // Drives each script's variableUpdate (where gameplay reads input). The server has no render frame,
  // so it runs this once per tick from the networked InputState; it must run before fixedUpdate so any
  // force the script queues from input is applied the same tick.
  void variableUpdate(ObjectManager& objectManager);

  // The scene-aware entry points below drive the scene's own scripts (SceneAsset::getScripts) as well as
  // its objects' scripts, in a fixed order: scene scripts start before object scripts, run variableUpdate
  // and fixedUpdate before them each tick, in list order, and stop after them. Scene scripts have their
  // own bookkeeping, so the ObjectManager overloads above never see or stop one.
  void start(SceneAsset& scene);

  void stop(SceneAsset& scene);

  void fixedUpdate(SceneAsset& scene, float dt);

  void variableUpdate(SceneAsset& scene);

  void attachAll(SceneAsset& scene);

  // Hot reload for a scene that is not ticking: the same throttled check fixedUpdate makes. When a reload
  // replaced the scripts, attaches the scene's scripts (without starting them) and refreshes their field
  // blobs, so a newly compiled class exposes its fields. Returns true then, so the caller can re-snapshot.
  [[nodiscard]] bool pollScriptChanges(SceneAsset& scene, float dt);

  // Also refreshes each scene script's blob, skipping a missing or faulted instance so saved values stay.
  void syncFieldsToData(const SceneAsset& scene) const;

  // The scene-script counterpart of applyScriptFieldEdit; does nothing when that class is not attached.
  void applySceneScriptFieldEdit(const uuids::uuid& sceneUUID,
                                 const std::string& className,
                                 const nlohmann::json& fields) const;

  // Deliver one collision event to both objects' scripts: objectA's scripts learn of objectB and vice
  // versa. Fed plain uuid pairs by the app (from CollisionSystem's enter/stay/exit lists) so scripting
  // stays independent of sim. Safe if an object was destroyed (its side is skipped); the survivor still
  // gets the event.
  void dispatchCollisionEvent(ObjectManager& objectManager,
                              const uuids::uuid& objectA,
                              const uuids::uuid& objectB,
                              CollisionEvent event) const;

  // Deliver one scene-wide event to every attached and started scene script of `scene`, in list order.
  // `object` names the spawned or destroyed object and `slot` the player for the events that carry one;
  // the other is passed to the script as empty / -1. Does nothing when no scene script has been started.
  // start(scene) delivers `loaded` and stop(scene) delivers `unloading` itself.
  void dispatchSceneEvent(SceneAsset& scene,
                          SceneEvent event,
                          const uuids::uuid* object = nullptr,
                          int slot = -1) const;

  // Attach any scripts that haven't been attached yet, without running start()/fixedUpdate on them.
  // Instances whose component has left the scene are stopped and detached first, so this does run
  // stop() on them. Call this before syncFieldsToData so newly added scripts have a field-cache entry.
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
  RuntimeFactory m_makeRuntime;
  std::vector<std::filesystem::path> m_scriptDirectories;
  std::unique_ptr<ScriptRuntime> m_engine;

  struct ExposedField {
    std::string name;
    std::string type;
    std::string displayName;
    std::string asset;
  };

  // The (uuid, class) pair behind a cache key, kept alongside it so an entry whose Script component is
  // gone can still be stopped and detached without parsing the key back apart. `component` identifies
  // which Script the instance was created for: a key alone cannot tell a surviving component from a
  // replacement of the same class on the same object.
  struct AttachedScript {
    uuids::uuid uuid;
    std::string className;
    std::weak_ptr<const Component> component;
  };

  // A scene script attached under its scene's uuid; `component` is the scene's Script entry it was created
  // for, so a replacement entry (SceneAsset::stop rebuilds them) orphans the old instance.
  struct AttachedSceneScript {
    uuids::uuid sceneUUID;
    std::string className;
    std::weak_ptr<const Component> component;
  };

  // key = uuid + "_" + className (cacheKey)
  std::unordered_map<std::string, AttachedScript> m_attached;
  // Mirrors m_attached by (uuid, class); updated at every insert, erase and clear of it.
  AttachedScripts m_attachedIndex;
  // Scripts whose class could not be instantiated. Left out of m_attached so nothing ticks or reads them,
  // and remembered so the attempt (and its warning) is not repeated every tick; cleared on hot reload.
  UnresolvedScripts m_unresolved;
  // Shared by object and scene scripts: a key leads with a scene or object uuid, which never collide.
  std::unordered_map<std::string, std::vector<ExposedField>> m_fieldCache;

  // Scene scripts are tracked apart from object scripts (same shape as m_attached/m_started/m_unresolved),
  // so neither orphan sweep can stop the other's instances. m_attachedIndex is object-only.
  std::unordered_map<std::string, AttachedSceneScript> m_attachedSceneScripts;
  std::unordered_set<std::string> m_startedSceneScripts;
  UnresolvedScripts m_unresolvedSceneScripts;

  // Instances that have had their C# start() called. attachAll() (run every broadcastSnapshot, whether
  // or not the scene is running) can attach a managed instance well before the scene's tick loop ever
  // sees it, so "attached" and "started" are tracked separately: fixedUpdate/variableUpdate start any
  // attached-but-not-yet-started instance the first time they would otherwise tick it.
  std::unordered_set<std::string> m_started;

  // Hot-reload: poll the user-script directory's write times a couple times a second; on change,
  // snapshot field values back into the data, reload the bridge, and let fixedUpdate re-attach lazily.
  using ScriptsSnapshot = std::unordered_map<std::string, std::filesystem::file_time_type>;
  ScriptsSnapshot m_scriptsSnapshot;
  float m_timeSinceLastSnapshot = 0.0f;

  void ensureEngine();

  // syncFields refreshes the data blobs of whatever the caller drives, right before a reload discards the
  // live instances.
  // Returns true when a reload replaced the scripts.
  bool checkForScriptChanges(const std::function<void()>& syncFields, float dt);

  // The per-object passes shared by the ObjectManager and SceneAsset entry points.
  void startObjects(ObjectManager& objectManager);

  void stopObjects(ObjectManager& objectManager);

  void fixedUpdateObjects(ObjectManager& objectManager, float dt);

  void variableUpdateObjects(ObjectManager& objectManager);

  void attachAllObjects(ObjectManager& objectManager);

  // Read a healthy instance's fields back into the script's blob; a missing or faulted one keeps it.
  void syncScriptBlob(const uuids::uuid& ownerUUID, const std::shared_ptr<Script>& script) const;

  // The scene-script twins of attach/detach/detachOrphans/startIfNeeded/isAttached. The orphan sweep
  // stops and detaches any scene script that left `scene`'s list, was replaced by a new Script entry, or
  // belongs to another scene.
  void attachSceneScript(const SceneAsset& scene, const std::shared_ptr<Script>& script);

  void detachSceneScript(const uuids::uuid& sceneUUID, const std::string& className);

  void detachSceneScriptOrphans(const SceneAsset& scene);

  void startSceneScriptIfNeeded(const uuids::uuid& sceneUUID, const std::string& className);

  [[nodiscard]] bool isSceneScriptAttached(const uuids::uuid& sceneUUID, const std::string& className) const;

  // Create the managed instance, cache its exposed fields, and push the Script's saved field blob in.
  // When the class cannot be instantiated the script stays unattached and its saved blob is untouched.
  // Takes the component by shared_ptr so the cache entry can remember which Script it belongs to.
  void attach(const Object& object, const std::shared_ptr<Script>& script);

  void detach(const uuids::uuid& uuid, const std::string& className);

  // Stop and detach every instance whose Script component is no longer in the scene, since every other
  // loop here only visits scripts the objects still return. Matching is by component identity, not by
  // key, so a replacement Script of the same class on the same object orphans the old instance rather
  // than silently inheriting it. Unresolved scripts whose component left or was replaced are forgotten too.
  void detachOrphans(const ObjectManager& objectManager);

  // Call the instance's C# start() exactly once - a no-op on every call after the first for the same
  // (uuid, className). Safe to call unconditionally from the running tick loop, whether the instance was
  // attached at scene start, lazily by fixedUpdate, or earlier by attachAll.
  void startIfNeeded(const uuids::uuid& uuid, const std::string& className);

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

  [[nodiscard]] ScriptsSnapshot takeSnapshot() const;
};



#endif //SCRIPTSYSTEM_H
