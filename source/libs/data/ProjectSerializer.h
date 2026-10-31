#ifndef PROJECTSERIALIZER_H
#define PROJECTSERIALIZER_H

#include <nlohmann/json_fwd.hpp>
#include <filesystem>
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

  // Throws std::runtime_error (before touching the live project) when the blob is not an object whose
  // "assets" is an object, so an empty or blanked file is an error rather than an empty project.
  void deserialize(const nlohmann::json& saveData) const;

  // Returns false (and logs why) if the file could not be written, so callers can avoid discarding
  // pending state on a save that didn't actually land.
  [[nodiscard]] bool save(const std::filesystem::path& path) const;

  // Returns false (and logs why) on a missing/malformed project, so callers can react instead of
  // silently running with no scene.
  [[nodiscard]] bool load(const std::string& path) const;

private:
  AssetRegistry* m_assetRegistry;

  SceneManager* m_sceneManager;

  std::shared_ptr<ComponentRegistry> m_componentRegistry;
};



#endif //PROJECTSERIALIZER_H
