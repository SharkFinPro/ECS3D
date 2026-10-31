#ifndef ASSETBROWSERPANEL_H
#define ASSETBROWSERPANEL_H

#include "AssetFilter.h"
#include <assets/AssetRegistry.h>
#include <nlohmann/json_fwd.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <uuid.h>

class AssetCreateFlow;
class AssetRegistry;
class GpuAssetCache;
class EditorSelection;

// The editor's "Assets" panel: a grid of the replicated AssetRegistry records with thumbnails (the
// texture image for textures, a labeled tile otherwise). Each tile is a drag-drop source carrying its
// uuid (so component editors can accept it); scene tiles are double-clicked to switch the active scene.
class AssetBrowserPanel {
public:
  using LoadSceneCallback = std::function<void(const uuids::uuid& sceneUUID)>;
  // The built addAsset blob ({ assetType, uuid, ... }) for the EditorApp to send to the server.
  using AddAssetCallback = std::function<void(const nlohmann::json& addAsset)>;

  AssetBrowserPanel(const AssetRegistry* assetRegistry, std::shared_ptr<GpuAssetCache> assetCache);

  ~AssetBrowserPanel();

  void setLoadSceneCallback(LoadSceneCallback callback);

  void setAddAssetCallback(AddAssetCallback callback);

  // Asked before an action writes into the project folder; true means it was handled (the project is an
  // untitled template) and nothing must be written.
  using BlockWritesCallback = std::function<bool()>;
  void setBlockWritesCallback(BlockWritesCallback callback);

  // The shared selection slot owned by EditorApp: a tile click selects that asset here (the same slot
  // the object tree/Inspector use), so selecting an asset deselects any object and vice versa.
  void setSelection(std::shared_ptr<EditorSelection> selection);

  // When false (the connected server isn't in edit mode), the asset grid still renders for browsing,
  // but creating assets and switching the active scene (both mutations) are disabled.
  void setEditable(bool editable);

  void displayGui();

  void displayMenuWidget();

private:
  // Switches to the scene commitAsset just created, if it created one. Called after the create popup
  // closes, so it is outside the boundary that reports a create as having failed.
  void activateCreatedScene();

  const AssetRegistry* m_assetRegistry;

  std::shared_ptr<GpuAssetCache> m_assetCache;

  // Shared with the object tree + Inspector; a tile click writes the Asset kind here.
  std::shared_ptr<EditorSelection> m_selection;

  LoadSceneCallback m_onLoadScene;
  AddAssetCallback m_onAddAsset;
  BlockWritesCallback m_blockWrites;

  AssetTypeFilter m_filter;

  char m_search[128] = {};

  std::unique_ptr<AssetCreateFlow> m_flow;

  enum class SortType { NameAscending, NameDescending, Type };
  SortType m_sortType = SortType::NameAscending;

  // Cached sorted+filtered view of the registry. Rebuilt only when m_dirty is true.
  std::vector<std::pair<uuids::uuid, AssetRecord>> m_cachedAssets;
  size_t m_lastRegistryVersion = SIZE_MAX; // force rebuild on first frame
  bool m_dirty = true;

  // False when the connected server is read-only (not in edit mode); gates create + scene switching.
  bool m_editable = true;

  void recomputeCache();

  [[nodiscard]] static const char* assetTypeLabel(AssetType type);

  [[nodiscard]] static std::string displayName(const AssetRecord& record);

  void displayAsset(const uuids::uuid& uuid, const AssetRecord& record, float cellSize, const std::string& name) const;

  // The import/create items shared by the menu bar, the Options row's "+" popup and the grid's
  // empty-space context menu. They only record the request; displayGui opens the modal.
  void displayCreateItems();

  void displayCreateAssetPopup();

  void commitAsset();
};



#endif //ASSETBROWSERPANEL_H
