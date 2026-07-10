#ifndef OBJECTGUIMANAGER_H
#define OBJECTGUIMANAGER_H

#include <nlohmann/json_fwd.hpp>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_set>
#include <uuid.h>

class ObjectManager;
class AssetRegistry;
class Object;
class Component;
class ComponentEditor;

// The editor's object tree ("Objects") + selected-object panel ("Selected Object"). It draws each
// component of the selected object through the ComponentEditor and reports edits back:
//   - a component VALUE change -> EditCallback(objectUUID, component)
//   - a STRUCTURAL change (add/remove object or component) -> SceneEditCallback(<built edit json>)
//   - a new prefab asset ("Save as Prefab") -> AddAssetCallback(<built addAsset json>)
// The EditorApp turns all three into network messages for the authoritative server.
class ObjectGUIManager {
public:
  using EditCallback = std::function<void(const uuids::uuid& objectUUID, const std::shared_ptr<Component>& component)>;
  using SceneEditCallback = std::function<void(const nlohmann::json& edit)>;
  // Same blob (and same EditorApp handler) as AssetBrowserPanel::AddAssetCallback.
  using AddAssetCallback = std::function<void(const nlohmann::json& addAsset)>;

  explicit ObjectGUIManager(std::shared_ptr<ComponentEditor> componentEditor);

  void setEditCallback(EditCallback callback);

  void setSceneEditCallback(SceneEditCallback callback);

  void setAddAssetCallback(AddAssetCallback callback);

  void setAssetRegistry(const AssetRegistry* registry);

  // objectManager may be null (no scene loaded yet): the windows are still drawn, just empty, so they
  // stay present/dockable instead of popping in and out.
  void displayGui(const ObjectManager* objectManager);

  // Set the selection externally (e.g. from viewport mouse-picking).
  void setSelectedObject(const std::optional<uuids::uuid>& objectUUID);

  // When false (the connected server isn't in edit mode), the panels still render so the scene can be
  // viewed/inspected, but the add/remove/reparent/edit affordances are disabled.
  void setEditable(bool editable);

  // The object to highlight in the viewport: the selection when "Highlight Object" is on, else nullopt.
  [[nodiscard]] std::optional<uuids::uuid> getHighlightUUID() const;

private:
  std::shared_ptr<ComponentEditor> m_componentEditor;

  EditCallback m_editCallback;
  SceneEditCallback m_sceneEditCallback;
  AddAssetCallback m_addAssetCallback;

  const AssetRegistry* m_assetRegistry = nullptr;

  // Buffer for in-place name editing. Refreshed whenever the selected object changes.
  std::array<char, 256> m_nameEditBuffer{};
  std::optional<uuids::uuid> m_nameEditObjectUUID;

  std::optional<uuids::uuid> m_selectedObject;

  // The object awaiting delete confirmation (set by the context-menu "Delete" or the Delete hotkey).
  // While set, the "Delete Object?" modal is shown; confirming sends a removeObject scene edit.
  std::optional<uuids::uuid> m_objectPendingDeletion;

  bool m_highlightSelectedObject = true;

  // False when the connected server is read-only (not in edit mode); gates the mutating UI.
  bool m_editable = true;

  // Components whose deletion we've already sent (markedAsDeleted persists until the next snapshot
  // rebuilds the object), so we don't re-send the same removeComponent every frame.
  std::unordered_set<const Component*> m_pendingRemovals;

  bool m_showComponentSelector = false;

  void displayObjectTree(const std::shared_ptr<Object>& object);

  void displaySelectedObject(const ObjectManager* objectManager);

  // The "Delete Object?" confirmation modal for m_objectPendingDeletion. Confirming (Yes / Enter) sends
  // a removeObject scene edit; cancelling (No / Escape), or the object vanishing, clears the prompt.
  void displayDeleteConfirmationModal(const ObjectManager* objectManager);

  void displayAddComponent(const std::shared_ptr<Object>& object);

  // Write the object's serialized blob to assets/prefabs/<Name>.prefab and register it as a Prefab asset.
  // Re-saving under the same name updates the file in place and keeps the existing asset record.
  void saveAsPrefab(const std::shared_ptr<Object>& object) const;

  void displayScriptDragDropArea(float dropZoneStartY, const std::shared_ptr<Object>& object) const;

  void displayComponent(const uuids::uuid& objectUUID, const std::shared_ptr<Component>& component);
};



#endif //OBJECTGUIMANAGER_H
