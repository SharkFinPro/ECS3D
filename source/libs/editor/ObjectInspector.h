#ifndef OBJECTINSPECTOR_H
#define OBJECTINSPECTOR_H

#include <nlohmann/json_fwd.hpp>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>
#include <uuid.h>

class AssetRegistry;
class Object;
class Component;
class ComponentEditor;

// The Inspector's renderer for the Object selection kind: the object's name field, Highlight toggle,
// component + script widgets (via ComponentEditor), Add Component, and the script drop zone. Extracted
// from ObjectGUIManager so the panel can dispatch per selection kind; it reports edits back through three
// callbacks:
//   - a component VALUE change -> EditCallback(objectUUID, component), every frame the value moves
//   - the same change once it is FINISHED -> EditCommittedCallback(objectUUID, before, after)
//   - a STRUCTURAL change (rename / add / remove component, add script) -> SceneEditCallback(<edit json>)
class ObjectInspector {
public:
  using EditCallback = std::function<void(const uuids::uuid& objectUUID, const std::shared_ptr<Component>& component)>;
  using SceneEditCallback = std::function<void(const nlohmann::json& edit)>;

  // One finished user action rather than one per frame: a widget edits the component in place, so a
  // slider drag fires EditCallback every frame and leaves nothing to read the pre-drag state from. The
  // pair here is the whole gesture - the value before the first frame of it, and the value it settled on.
  using EditCommittedCallback = std::function<void(const uuids::uuid& objectUUID,
                                                   const nlohmann::json& before,
                                                   const nlohmann::json& after)>;

  explicit ObjectInspector(std::shared_ptr<ComponentEditor> componentEditor);

  void setEditCallback(EditCallback callback);

  void setEditCommittedCallback(EditCommittedCallback callback);

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

  // The multi-selection body: the components common to every object in `objects` (by componentSignature -
  // a collider's subtype, a script's class name), each rendered once against the first object and applied
  // to the rest as a field-level delta (see ComponentFieldDelta.h). No Add/Remove Component or name field:
  // those would act on a single object, so they are omitted rather than silently applying to one.
  // `objects` must hold at least two objects.
  void displayMulti(const std::vector<std::shared_ptr<Object>>& objects);

  // Hands any gathered before/after pair(s) to the committed-edit callback and clears them - both the
  // single-selection gesture and any multi-selection one in flight. display()/displayMulti() do this
  // themselves once a gesture ends; the panel has to call it on the frames it does NOT reach either (
  // nothing selected, an asset selected, the selected object gone), or the pair would sit there and be
  // reported against whatever is inspected next.
  void commitPendingEdit();

  [[nodiscard]] bool highlightEnabled() const { return m_highlightObject; }

private:
  std::shared_ptr<ComponentEditor> m_componentEditor;

  EditCallback m_editCallback;
  EditCommittedCallback m_editCommittedCallback;
  SceneEditCallback m_sceneEditCallback;

  // The edit being gathered: which component is moving and what it looked like before the gesture
  // started. The blobs are stored dumped, for the same reason EditCommand stores its own that way - this
  // header only forward-declares nlohmann::json.
  struct PendingEdit {
    uuids::uuid objectUUID;
    std::weak_ptr<Component> component;
    std::string before;
    std::string after;
  };

  std::optional<PendingEdit> m_pendingEdit;

  // The multi-selection counterpart of m_pendingEdit: one entry per object sharing the component
  // signature currently being gathered (m_multiPendingSignature says which). Cleared together on commit.
  std::optional<std::string> m_multiPendingSignature;
  std::vector<PendingEdit> m_multiPendingEdits;

  // A cheap fingerprint of the multi-selection's object uuids (order-preserving), so displayMulti can tell
  // the selection changed and flush whatever gesture was in flight before it attaches to the new one.
  std::optional<std::string> m_multiSelectionKey;

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
  // rebuilds the object), so we don't re-send the same removeComponent every frame. Keyed by the
  // owning object's uuid plus the component's type identity (see componentIdentity in the .cpp),
  // never by address: an address can be reused by a same-size allocation right after a free
  // (TransientObject::rebuild, replication::applySceneEdit's add/remove component path), which would
  // silently swallow a later removal for an unrelated component landing at the same address. Entries
  // are dropped once confirmed (the component's slot is gone from the object) and on selection change.
  std::unordered_set<std::string> m_pendingRemovals;

  bool m_showComponentSelector = false;

  void displayAddComponent(const std::shared_ptr<Object>& object);

  void displayScriptDragDropArea(float dropZoneStartY, const std::shared_ptr<Object>& object) const;

  void displayComponent(const uuids::uuid& objectUUID, const std::shared_ptr<Component>& component);

  // Renders one component signature common to every object in `objects` against objects[0]'s component,
  // then propagates whatever top-level field changed to the rest of `objects`' own components (see
  // ComponentFieldDelta.h). `objects` is the whole multi-selection, all of which are guaranteed to carry
  // `signature` (displayMulti only calls this for common signatures).
  void displayMultiComponent(const std::vector<std::shared_ptr<Object>>& objects, const std::string& signature);

  // Flushes m_multiPendingEdits: one EditCommittedCallback per object whose before/after actually differ.
  void commitPendingMultiEdit();
};

#endif //OBJECTINSPECTOR_H
