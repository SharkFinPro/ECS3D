#ifndef OBJECTGUIMANAGER_H
#define OBJECTGUIMANAGER_H

#include <nlohmann/json_fwd.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <uuid.h>

class ObjectManager;
class Object;
class EditorSelection;

// The editor's object tree ("Objects" panel): the hierarchy, its per-row context menu, "Save as Prefab",
// and the delete-confirmation modal. It reports structural changes back:
//   - a STRUCTURAL change (add/remove/reparent/duplicate object) -> SceneEditCallback(<built edit json>)
//   - a new prefab asset ("Save as Prefab") -> AddAssetCallback(<built addAsset json>)
// The EditorApp turns both into network messages for the authoritative server. The selected object's
// body is inspected by the separate InspectorPanel; both share the one EditorSelection slot.
class ObjectGUIManager {
public:
  using SceneEditCallback = std::function<void(const nlohmann::json& edit)>;
  // Same blob (and same EditorApp handler) as AssetBrowserPanel::AddAssetCallback.
  using AddAssetCallback = std::function<void(const nlohmann::json& addAsset)>;

  void setSceneEditCallback(SceneEditCallback callback);

  void setAddAssetCallback(AddAssetCallback callback);

  // The shared selection slot owned by EditorApp: the tree click and delete flows read/write it. Other
  // writers (viewport picking, the Inspector) share the same instance, so selecting an object here and
  // picking one there stay in sync.
  void setSelection(std::shared_ptr<EditorSelection> selection);

  // objectManager may be null (no scene loaded yet): the window is still drawn, just empty, so it stays
  // present/dockable instead of popping in and out.
  void displayGui(const ObjectManager* objectManager);

  // When false (the connected server isn't in edit mode), the tree still renders so the scene can be
  // viewed/inspected, but the add/remove/reparent affordances are disabled.
  void setEditable(bool editable);

private:
  SceneEditCallback m_sceneEditCallback;
  AddAssetCallback m_addAssetCallback;

  // The editor-wide selection, owned by EditorApp and shared with the other panels. The Object kind is
  // what this manager cares about; asset selections are inspected by other panels.
  std::shared_ptr<EditorSelection> m_selection;

  // The object awaiting delete confirmation (set by the context-menu "Delete" or the Delete hotkey).
  // While set, the "Delete Object?" modal is shown; confirming sends a removeObject scene edit.
  std::optional<uuids::uuid> m_objectPendingDeletion;

  // False when the connected server is read-only (not in edit mode); gates the mutating UI.
  bool m_editable = true;

  void displayObjectTree(const std::shared_ptr<Object>& object);

  // The "Delete Object?" confirmation modal for m_objectPendingDeletion. Confirming (Yes / Enter) sends
  // a removeObject scene edit; cancelling (No / Escape), or the object vanishing, clears the prompt.
  void displayDeleteConfirmationModal(const ObjectManager* objectManager);

  // Register the object's serialized blob as a Prefab asset (body carried inline, no file on disk).
  // Re-saving under the same name updates that prefab's body in place, keeping its uuid.
  void saveAsPrefab(const std::shared_ptr<Object>& object) const;
};



#endif //OBJECTGUIMANAGER_H
