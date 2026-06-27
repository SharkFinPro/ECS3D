#ifndef ASSETBROWSERPANEL_H
#define ASSETBROWSERPANEL_H

#include <assets/AssetRegistry.h>
#include <nlohmann/json_fwd.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <uuid.h>

class AssetRegistry;
class GpuAssetCache;

// The editor's "Assets" panel: a grid of the replicated AssetRegistry records with thumbnails (the
// texture image for textures, a labeled tile otherwise). Each tile is a drag-drop source carrying its
// uuid (so component editors can accept it); scene tiles are double-clicked to switch the active scene.
class AssetBrowserPanel {
public:
  using LoadSceneCallback = std::function<void(const uuids::uuid& sceneUUID)>;
  // The built addAsset blob ({ assetType, uuid, ... }) for the EditorApp to send to the server.
  using AddAssetCallback = std::function<void(const nlohmann::json& addAsset)>;

  AssetBrowserPanel(const AssetRegistry* assetRegistry, std::shared_ptr<GpuAssetCache> assetCache);

  void setLoadSceneCallback(LoadSceneCallback callback);

  void setAddAssetCallback(AddAssetCallback callback);

  // When false (the connected server isn't in edit mode), the asset grid still renders for browsing,
  // but creating assets and switching the active scene (both mutations) are disabled.
  void setEditable(bool editable);

  void displayGui();

  void displayMenuWidget();

private:
  // A model/texture import or scene/script creation awaiting a name in the create popup.
  struct PendingAsset {
    enum class Type { None, Model, Texture, Scene, Script } type = Type::None;
    std::string sourcePath; // the picked file, for Model/Texture
    char name[128] = {};
  };

  const AssetRegistry* m_assetRegistry;

  std::shared_ptr<GpuAssetCache> m_assetCache;

  LoadSceneCallback m_onLoadScene;
  AddAssetCallback m_onAddAsset;

  AssetType m_filter = AssetType::Unknown;

  char m_search[128] = {};

  PendingAsset m_pending;
  bool m_openCreatePopup = false;
  std::string m_createError;

  enum class SortType { NameAscending, NameDescending };
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

  void displayCreateAssetPopup();

  void commitAsset();
};



#endif //ASSETBROWSERPANEL_H
