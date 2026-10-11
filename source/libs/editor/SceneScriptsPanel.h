#ifndef SCENESCRIPTSPANEL_H
#define SCENESCRIPTSPANEL_H

#include "SceneScriptsPanelLogic.h"
#include <nlohmann/json.hpp>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
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

  // See FieldEditTracker: the replicated Script stays untouched while a widget is held so the undo
  // entry's "before" is still derived from the replicated value.
  sceneScriptPanel::FieldEditTracker m_fieldEdits;
  sceneScriptPanel::StructuralEditGate m_structuralGate;
  bool m_structuralPending = false;

  void sendStructural(const nlohmann::json& edit, const SceneAsset& scene);

  void send(const nlohmann::json& edit) const;

  void syncSceneTracking(const std::shared_ptr<SceneAsset>& scene);

  void displayContents(const SceneAsset* scene);

  void displayRow(const SceneAsset& scene, std::size_t index);

  void displayArrowButton(const SceneAsset& scene, std::size_t index, bool up);

  void displayAddControls(const SceneAsset& scene);

  void displayDropTarget(const SceneAsset& scene);

  void flushFieldEdits(const SceneAsset* scene);

  [[nodiscard]] static std::vector<std::string> classOrder(const SceneAsset* scene);
};

#endif //SCENESCRIPTSPANEL_H
