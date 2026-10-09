#ifndef SCENEASSET_H
#define SCENEASSET_H

#include <nlohmann/json.hpp>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <uuid.h>

namespace net {
  class Message;
  class MessageReader;
}

class ObjectManager;
class ComponentRegistry;
class Script;

// A scene is data: a named ObjectManager (the object tree). It is not part of the polymorphic Asset
// hierarchy (the file assets are flat records in AssetRegistry); behavior lives in the systems and
// displayGui in the editor. start/stop remain here as data lifecycle for the play/stop ComponentVariables.
class SceneAsset {
public:
  SceneAsset(uuids::uuid uuid,
             std::string name,
             const std::shared_ptr<ComponentRegistry>& componentRegistry);

  void loadObjects(const nlohmann::json& objectsData) const;

  // Replaces the scene script list from a serialized "scripts" array. An entry that is malformed or repeats
  // a class already loaded is skipped with a warning, so a bad entry degrades the scene instead of failing it.
  void loadScripts(const nlohmann::json& scriptsData);

  [[nodiscard]] const std::vector<std::shared_ptr<Script>>& getScripts() const;

  [[nodiscard]] std::shared_ptr<Script> findScript(const std::string& className) const;

  // Refuses (false, list unchanged) a null script, an empty class name, or a class already present. An index
  // past the end appends.
  bool addScript(const std::shared_ptr<Script>& script, std::optional<std::size_t> index = std::nullopt);

  bool removeScript(const std::string& className);

  // The index is read against the list after the script is removed from it. Refused (false) for an unknown
  // class, an index past the end of that list, or a move that leaves the script where it is.
  bool moveScript(const std::string& className, std::size_t index);

  // Captures the authored object tree before the run starts, so stop() can undo whatever the run did
  // structurally (spawn/destroy/reparent) on top of the values it already resets.
  void start();

  void stop();

  [[nodiscard]] nlohmann::json serialize() const;

  void pack(net::Message& message) const;

  // Reconstructs a scene (uuid + name + object tree) from a packed snapshot. A static factory because
  // the uuid/name lead the packed data and are needed to construct the SceneAsset itself.
  [[nodiscard]] static std::shared_ptr<SceneAsset> unpack(net::MessageReader& messageReader,
                                                          const std::shared_ptr<ComponentRegistry>& componentRegistry);

  [[nodiscard]] std::shared_ptr<ObjectManager> getObjectManager() const;

  [[nodiscard]] uuids::uuid getUUID() const;

  [[nodiscard]] std::string getName() const;

  // Used to enforce unique scene names (SceneManager::uniqueSceneName) at the registration sites,
  // before the scene's AssetRegistry record is keyed off it - keeps `getName()` and that key in sync.
  void setName(std::string name);

private:
  uuids::uuid m_uuid;

  std::string m_name;

  std::shared_ptr<ObjectManager> m_objectManager;

  // The scene's authored object data (the "objects" array ObjectManager::serialize() would write), taken
  // by start() and consumed by stop(). Null when no run is in progress to restore from.
  nlohmann::json m_authoredObjects;

  // Execution order among the scene's scripts; at most one entry per class name.
  std::vector<std::shared_ptr<Script>> m_scripts;

  // The serialized script list taken by start() and consumed by stop(). Null when no run is in progress.
  nlohmann::json m_authoredScripts;

  [[nodiscard]] nlohmann::json serializeScripts() const;
};



#endif //SCENEASSET_H
