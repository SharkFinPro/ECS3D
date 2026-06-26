#ifndef ASSETBROWSERPANEL_H
#define ASSETBROWSERPANEL_H

#include <assets/AssetRegistry.h>
#include <nlohmann/json_fwd.hpp>
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
  // The built addAsset blob ({ assetType, uuid, ... }) for the EditorApp to send to the server.
  using AddAssetCallback = std::function<void(const nlohmann::json& addAsset)>;

  AssetBrowserPanel(const AssetRegistry* assetRegistry, std::shared_ptr<GpuAssetCache> assetCache);

  void setLoadSceneCallback(LoadSceneCallback callback);

  void setAddAssetCallback(AddAssetCallback callback);

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

  [[nodiscard]] static const char* assetTypeLabel(AssetType type);

  [[nodiscard]] static std::string displayName(const AssetRecord& record);

  void displayAsset(const uuids::uuid& uuid, const AssetRecord& record, float cellSize, const std::string& name);

  void displayCreateAssetPopup();

  void commitAsset();
};



#endif //ASSETBROWSERPANEL_H
