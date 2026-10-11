#ifndef SCENESCRIPTSPANEL_H
#define SCENESCRIPTSPANEL_H

#include <nlohmann/json.hpp>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <uuid.h>

class AssetRegistry;
class SceneAsset;
class SceneManager;

// The editor's "Scene Scripts" window: the current scene's ordered script list, with add, remove, reorder
// and exposed-field editing. It sends the scene-script sceneEdit ops through the scene-edit callback and
// leaves applying them to the rebroadcast snapshot, like every other structural edit.
class SceneScriptsPanel {
public:
  using SceneEditCallback = std::function<void(const nlohmann::json& edit)>;

  void setAssetRegistry(const AssetRegistry* registry);

  void setSceneManager(const SceneManager* sceneManager);

  void setSceneEditCallback(SceneEditCallback callback);

  void setEditable(bool editable);

  void displayGui();

  // Shows the panel and asks ImGui to focus it, matching the Console's Window-menu behavior.
  void open();

  [[nodiscard]] bool isOpen() const;

  void setOpen(bool open);

private:
  const AssetRegistry* m_assetRegistry = nullptr;
  const SceneManager* m_sceneManager = nullptr;
  SceneEditCallback m_sceneEditCallback;

  bool m_editable = true;
  bool m_open = true;
  bool m_focusRequested = false;

  std::optional<uuids::uuid> m_sceneUUID;

  // Field blobs being edited, by class name. The replicated Script stays untouched while a widget is held
  // so the undo entry's "before" is still derived from the replicated value.
  std::unordered_map<std::string, nlohmann::json> m_editCopies;

  void send(const nlohmann::json& edit) const;

  void displayAddButton(const SceneAsset& scene);

  void displayDropTarget(const SceneAsset& scene) const;

  void flushEditCopies(const SceneAsset& scene);
};

#endif //SCENESCRIPTSPANEL_H
