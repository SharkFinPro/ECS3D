#ifndef PROJECTSERIALIZER_H
#define PROJECTSERIALIZER_H

#include <nlohmann/json_fwd.hpp>
#include <memory>
#include <string>

class AssetRegistry;
class SceneManager;
class ComponentRegistry;

// serialize()/deserialize() produce the JSON blob used as both the save file and the network
// Snapshot (full state on join); save()/load() add file I/O on top.
class ProjectSerializer {
public:
  ProjectSerializer(AssetRegistry* assetRegistry,
                    SceneManager* sceneManager,
                    std::shared_ptr<ComponentRegistry> componentRegistry);

  [[nodiscard]] nlohmann::json serialize() const;

  void deserialize(const nlohmann::json& saveData) const;

  void save(const std::string& path) const;

  // Returns false (and logs why) on a missing/malformed project, so callers can react instead of
  // silently running with no scene.
  [[nodiscard]] bool load(const std::string& path) const;

private:
  AssetRegistry* m_assetRegistry;

  SceneManager* m_sceneManager;

  std::shared_ptr<ComponentRegistry> m_componentRegistry;
};



#endif //PROJECTSERIALIZER_H
