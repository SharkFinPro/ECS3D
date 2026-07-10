#ifndef INSPECTORPANEL_H
#define INSPECTORPANEL_H

#include <nlohmann/json_fwd.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <uuid.h>

class ObjectManager;
class AssetRegistry;
class Component;
class ComponentEditor;
class GpuAssetCache;
class EditorSelection;
class ObjectInspector;
class AssetInspector;

// The editor's inspector panel (the old "Selected Object" window), generalised into a per-kind
// dispatcher: it owns the window, the empty state, and dispatch on the shared EditorSelection's kind.
// Today only the Object kind has a renderer (delegated to ObjectInspector); later phases register
// asset-type renderers behind the same panel. The object edit callbacks + ComponentEditor are handed
// straight to ObjectInspector, wired exactly as they were on ObjectGUIManager.
class InspectorPanel {
public:
  using EditCallback = std::function<void(const uuids::uuid& objectUUID, const std::shared_ptr<Component>& component)>;
  using SceneEditCallback = std::function<void(const nlohmann::json& edit)>;

  InspectorPanel(std::shared_ptr<ComponentEditor> componentEditor, std::shared_ptr<GpuAssetCache> assetCache);

  ~InspectorPanel();

  // The shared selection slot owned by EditorApp (same instance the object tree + viewport picking
  // write): the panel dispatches on its kind and the highlight derives from it.
  void setSelection(std::shared_ptr<EditorSelection> selection);

  // Used both to resolve an Asset selection to its record (and clear it when the asset leaves the
  // registry) and by the object inspector's script drop zone.
  void setAssetRegistry(const AssetRegistry* registry);

  void setEditable(bool editable);

  void setEditCallback(EditCallback callback);

  void setSceneEditCallback(SceneEditCallback callback);

  // objectManager may be null (no scene loaded yet): the window is still drawn (empty) so it stays
  // present/dockable instead of popping in and out.
  void displayGui(const ObjectManager* objectManager);

  // The object to highlight in the viewport: the object selection when the object inspector's Highlight
  // toggle is on, else nullopt (also nullopt for the None/Asset kinds).
  [[nodiscard]] std::optional<uuids::uuid> getHighlightUUID() const;

private:
  std::shared_ptr<EditorSelection> m_selection;

  const AssetRegistry* m_assetRegistry = nullptr;

  std::unique_ptr<ObjectInspector> m_objectInspector;
  std::unique_ptr<AssetInspector> m_assetInspector;

  // The registry version last checked for a stale asset selection. Re-validation only runs when the
  // registry changes (matching the asset browser's cache gating).
  size_t m_lastAssetRegistryVersion = SIZE_MAX;
};

#endif //INSPECTORPANEL_H
