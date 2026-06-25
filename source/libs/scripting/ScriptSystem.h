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

// Drives the live C# script instances on the server. It owns the ScriptEngine (the ScriptBridge ABI)
// and absorbs the old ScriptManager's bookkeeping: which (uuid, class) pairs are attached, the
// exposed-field cache, and the hot-reload file snapshot. Each Script data component just carries a
// field blob; ScriptSystem syncs that blob <-> the live instance (writes it on attach/start, reads it
// back before a snapshot). Gameplay only ever runs here, on the server.
class ScriptSystem {
public:
  explicit ScriptSystem(std::shared_ptr<ManagedHost> host);

  ~ScriptSystem();

  // Attach + start every Script's managed instance, pushing each Script's saved field blob into it.
  // Mirrors the old scene-start path (Object::start -> Script::start).
  void start(ObjectManager& objectManager);

  // Stop + detach every managed instance.
  void stop(ObjectManager& objectManager);

  void fixedUpdate(ObjectManager& objectManager, float dt);

  // Refresh every Script's field blob from its live instance so a serialize()/Snapshot carries the
  // current values. Call this before building a Snapshot.
  void syncFieldsToData(ObjectManager& objectManager);

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

  void checkForScriptChanges(ObjectManager& objectManager, float dt);

  // Create the managed instance, cache its exposed fields, and push the Script's saved field blob in.
  void attach(const Object& object, const Script& script);

  void detach(const uuids::uuid& uuid, const std::string& className);

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
