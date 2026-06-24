#ifndef PROJECTSERIALIZER_H
#define PROJECTSERIALIZER_H

#include <memory>
#include <string>

class AssetRegistry;
class SceneManager;
class ComponentRegistry;

// The load/save half of the old SaveManager (no file dialogs / keybinds — those are the editor's
// SaveUI). It writes the project JSON by merging the AssetRegistry (model/texture/script records)
// with the SceneManager's scenes, and loads it back into the same.
class ProjectSerializer {
public:
  ProjectSerializer(AssetRegistry* assetRegistry,
                    SceneManager* sceneManager,
                    std::shared_ptr<ComponentRegistry> componentRegistry);

  void save(const std::string& path) const;

  void load(const std::string& path) const;

private:
  AssetRegistry* m_assetRegistry;

  SceneManager* m_sceneManager;

  std::shared_ptr<ComponentRegistry> m_componentRegistry;
};



#endif //PROJECTSERIALIZER_H
