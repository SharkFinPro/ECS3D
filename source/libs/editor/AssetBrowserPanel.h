#ifndef ASSETBROWSERPANEL_H
#define ASSETBROWSERPANEL_H

#include <assets/AssetRegistry.h>
#include <functional>
#include <memory>
#include <string>
#include <uuid.h>

class AssetRegistry;
class GpuAssetCache;

// The editor's "Assets" panel: a grid of the replicated AssetRegistry records with thumbnails (the
// texture image for textures, a labelled tile otherwise). Each tile is a drag-drop source carrying its
// uuid (so component editors can accept it); scene tiles are double-clicked to switch the active scene.
class AssetBrowserPanel {
public:
  using LoadSceneCallback = std::function<void(const uuids::uuid& sceneUUID)>;

  AssetBrowserPanel(const AssetRegistry* assetRegistry, std::shared_ptr<GpuAssetCache> assetCache);

  void setLoadSceneCallback(LoadSceneCallback callback);

  void displayGui();

  void displayMenuWidget();

private:
  const AssetRegistry* m_assetRegistry;

  std::shared_ptr<GpuAssetCache> m_assetCache;

  LoadSceneCallback m_onLoadScene;

  AssetType m_filter = AssetType::Unknown;

  char m_search[128] = {};

  [[nodiscard]] static const char* assetTypeLabel(AssetType type);

  [[nodiscard]] static std::string displayName(const AssetRecord& record);

  void displayAsset(const uuids::uuid& uuid, const AssetRecord& record, float cellSize, const std::string& name);
};



#endif //ASSETBROWSERPANEL_H
