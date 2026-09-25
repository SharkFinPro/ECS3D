#ifndef OBJECTGUIMANAGER_H
#define OBJECTGUIMANAGER_H

#include <nlohmann/json_fwd.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <vector>
#include <uuid.h>

class ObjectManager;
class Object;
class EditorSelection;
class SettingsStore;

// The editor's object tree ("Objects" panel): the hierarchy, its per-row context menu, and "Save as Prefab".
// It reports structural changes back:
//   - a STRUCTURAL change (add/remove/reparent/duplicate object) -> SceneEditCallback(<built edit json>)
//   - a new prefab asset ("Save as Prefab") -> AddAssetCallback(<built addAsset json>)
// The EditorApp turns both into network messages for the authoritative server. The selected object's
// body is inspected by the separate InspectorPanel; both share the one EditorSelection slot.
class ObjectGUIManager {
public:
  using SceneEditCallback = std::function<void(const nlohmann::json& edit)>;
  // Same blob (and same EditorApp handler) as AssetBrowserPanel::AddAssetCallback.
  using AddAssetCallback = std::function<void(const nlohmann::json& addAsset)>;

  // How the tree orders siblings for display. This never touches the scene: authored is the order
  // ObjectManager/Object already hand out (today, load order - there is no persisted sibling order yet),
  // and alphabetical is a display-only sorted copy built fresh each frame.
  enum class SortMode { authored, alphabetical };

  // The store this panel's sort preference is read from and written to. Optional: with none set (or
  // passed null) the panel just keeps authored order and never persists a choice. Reads the stored mode
  // immediately, the way SettingsPanel's own constructor does for its section state.
  void setSettings(SettingsStore* settings);

  [[nodiscard]] SortMode sortMode() const;

  void setSortMode(SortMode mode);

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

  // Deletes the current selection (the Delete keybind's handler - the context menu and row minus button
  // funnel into the same private performDelete()). No-op unless the selection is of
  // Object kind, non-empty, and m_editable.
  void requestDeleteSelection();

  // Duplicates the current selection (the Ctrl+D keybind's handler). Same guard as
  // requestDeleteSelection(). A single selected object sends a plain duplicateObject op; several send one
  // batch of duplicateObject ops for the top-most selected objects (an object whose ancestor is also
  // selected is skipped - duplicating the ancestor already copies it). The selection itself is unchanged.
  // objectManager resolves the selection's uuids to Objects to find those top-most ancestors.
  void duplicateSelection(const ObjectManager* objectManager);

private:
  SceneEditCallback m_sceneEditCallback;
  AddAssetCallback m_addAssetCallback;

  // The editor-wide selection, owned by EditorApp and shared with the other panels. The Object kind is
  // what this manager cares about; asset selections are inspected by other panels.
  std::shared_ptr<EditorSelection> m_selection;

  // False when the connected server is read-only (not in edit mode); gates the mutating UI.
  bool m_editable = true;

  // Owned by EditorApp and valid for as long as this panel is being drawn. Null until setSettings() is
  // called. Not safe to touch during EditorApp teardown: EditorApp destroys its SettingsStore member
  // before its ObjectGUIManager member, so this pointer is dangling by the time this manager's own
  // destructor would run. Inert today only because there is no destructor logic here - a future
  // destructor or flush-on-close hook must not read or write through m_settings.
  SettingsStore* m_settings = nullptr;

  SortMode m_sortMode = SortMode::authored;

  // The object being dragged, resolved once per frame and cleared at the end of it.
  std::shared_ptr<Object> m_dragSource;

  // The tree's current display order (depth-first, respecting sort mode and which nodes are open) -
  // rebuilt fresh every displayGui() call. A Shift-click range is measured against this, not the scene's
  // own object graph, so it always matches what is actually on screen (a row hidden behind a collapsed
  // ancestor is not part of a range spanning past it).
  std::vector<uuids::uuid> m_visibleOrder;

  // The uuid a Shift-click range is measured from: the last row a plain or Ctrl-click landed on. Shift
  // and Ctrl+Shift-click leave it alone, so repeated range-selects keep extending from the same start.
  std::optional<uuids::uuid> m_rangeAnchor;

  // A row click captured during this frame's tree traversal and applied once the traversal (and so
  // m_visibleOrder) is complete - a Shift-click range needs the full display order, which isn't known
  // until every row has been visited.
  struct PendingClick {
    uuids::uuid uuid;
    bool ctrl;
    bool shift;
  };
  std::optional<PendingClick> m_pendingClick;

  // The small sort-mode picker drawn in the panel header: a "Sort" label, a button naming the current
  // mode, and a popup to switch it.
  void displaySortControl();

  void displayObjectTree(const std::shared_ptr<Object>& object);

  // Applies m_pendingClick (if any) against the now-complete m_visibleOrder, then clears it. See
  // m_pendingClick for why this waits until after the whole tree has been traversed.
  void applyPendingClick();

  // False for a target (a row, for a reparent-onto-it drop, or a sibling list's own parent - null for the
  // scene root - for a reorder-between-siblings drop) the current drag must not be dropped onto, so it
  // registers no drop target and ImGui shows the drag its own "not allowed" cursor instead of a silent
  // no-op once released.
  [[nodiscard]] bool canAcceptObjectDrop(const std::shared_ptr<Object>& target) const;

  // A thin drop target for reordering, placed before the first sibling, between each pair, and after the
  // last, so a drag can land BETWEEN two siblings at a specific position instead of only being reparented
  // onto a row (which always appends). parent is the sibling list's own parent (null for the scene root);
  // index is that list's storage position the object would occupy if the list were untouched - the drop
  // handler itself accounts for the slot the dragged object vacates when it is already in this same list.
  // Skipped entirely outside authored sort mode: alphabetical display order does not match the list an
  // index addresses, so a zone there could not honestly promise where the drop would land.
  void displayReorderDropZone(const std::shared_ptr<Object>& parent, std::size_t index);

  // Register the object's serialized blob as a Prefab asset (body carried inline, no file on disk).
  // Re-saving under the same name updates that prefab's body in place, keeping its uuid.
  void saveAsPrefab(const std::shared_ptr<Object>& object) const;

  // row alone, unless row is part of a multi-object selection - then the whole selection, in selection
  // order. Shared by the context menu, the row minus/duplicate button, and requestDeleteSelection()/
  // duplicateSelection() (which always act on the whole current selection, with no particular row).
  [[nodiscard]] std::vector<uuids::uuid> targetsFor(const std::shared_ptr<Object>& row) const;

  // Sends targets as one removeObject op (a single target) or one batch of them (several), then drops
  // them from the selection - the shared tail of requestDeleteSelection() and the context-menu/row-button
  // Delete paths.
  void performDelete(const std::vector<uuids::uuid>& targets);

  // Sends targets as one duplicateObject op (a single target) or one batch of them (several, after
  // dropping any target whose ancestor is also a target) - the shared tail of duplicateSelection() and
  // the context-menu Duplicate path.
  void performDuplicate(const ObjectManager* objectManager, const std::vector<uuids::uuid>& targets);
};



#endif //OBJECTGUIMANAGER_H
