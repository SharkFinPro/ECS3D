#ifndef PROJECTSERIALIZER_H
#define PROJECTSERIALIZER_H

#include <nlohmann/json_fwd.hpp>
#include <memory>
#include <string>

class AssetRegistry;
class SceneManager;
class ComponentRegistry;

// The load/save half of the old SaveManager (no file dialogs / keybinds — those are the editor's
// SaveUI). serialize()/deserialize() are JSON-only so the same project blob is reused as the network
// Snapshot (full state on join); save()/load() just add file I/O on top.
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
  bool load(const std::string& path) const;

private:
  AssetRegistry* m_assetRegistry;

  SceneManager* m_sceneManager;

  std::shared_ptr<ComponentRegistry> m_componentRegistry;
};



#endif //PROJECTSERIALIZER_H
