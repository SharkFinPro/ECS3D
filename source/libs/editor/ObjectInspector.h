#ifndef OBJECTINSPECTOR_H
#define OBJECTINSPECTOR_H

#include <nlohmann/json_fwd.hpp>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_set>
#include <uuid.h>

class AssetRegistry;
class Object;
class Component;
class ComponentEditor;

// The Inspector's renderer for the Object selection kind: the object's name field, Highlight toggle,
// component + script widgets (via ComponentEditor), Add Component, and the script drop zone. Extracted
// from ObjectGUIManager so the panel can dispatch per selection kind; it reports edits back through two
// callbacks:
//   - a component VALUE change -> EditCallback(objectUUID, component)
//   - a STRUCTURAL change (rename / add / remove component, add script) -> SceneEditCallback(<edit json>)
class ObjectInspector {
public:
  using EditCallback = std::function<void(const uuids::uuid& objectUUID, const std::shared_ptr<Component>& component)>;
  using SceneEditCallback = std::function<void(const nlohmann::json& edit)>;

  explicit ObjectInspector(std::shared_ptr<ComponentEditor> componentEditor);

  void setEditCallback(EditCallback callback);

  void setSceneEditCallback(SceneEditCallback callback);

  void setAssetRegistry(const AssetRegistry* registry);

  void setEditable(bool editable);

  // When false, the "Highlight Object" checkbox at the top is omitted. The Highlight toggle only means
  // something for a live scene object (it drives the viewport highlight); the AssetInspector reuses this
  // inspector to edit a detached prefab body, where there is nothing to highlight.
  void setShowHighlightToggle(bool show);

  // The right-aligned type chip in the panel header (icon pill), drawn on the current header row.
  void displayTypeChip(const std::shared_ptr<Object>& object) const;

  // The object body: Highlight toggle, name, components, Add Component, scripts + drop zone.
  void display(const std::shared_ptr<Object>& object);

  [[nodiscard]] bool highlightEnabled() const { return m_highlightObject; }

private:
  std::shared_ptr<ComponentEditor> m_componentEditor;

  EditCallback m_editCallback;
  SceneEditCallback m_sceneEditCallback;

  const AssetRegistry* m_assetRegistry = nullptr;

  // False when the connected server is read-only (not in edit mode); gates the mutating UI.
  bool m_editable = true;

  // Buffer for in-place name editing. Refreshed whenever the selected object changes.
  std::array<char, 256> m_nameEditBuffer{};
  std::optional<uuids::uuid> m_nameEditObjectUUID;

  bool m_highlightObject = true;

  // Whether to draw the Highlight toggle (see setShowHighlightToggle).
  bool m_showHighlightToggle = true;

  // Components whose deletion we've already sent (markedAsDeleted persists until the next snapshot
  // rebuilds the object), so we don't re-send the same removeComponent every frame.
  std::unordered_set<const Component*> m_pendingRemovals;

  bool m_showComponentSelector = false;

  void displayAddComponent(const std::shared_ptr<Object>& object);

  void displayScriptDragDropArea(float dropZoneStartY, const std::shared_ptr<Object>& object) const;

  void displayComponent(const uuids::uuid& objectUUID, const std::shared_ptr<Component>& component);
};

#endif //OBJECTINSPECTOR_H
