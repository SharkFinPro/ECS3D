#ifndef ASSETREGISTRY_H
#define ASSETREGISTRY_H

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <unordered_map>
#include <uuid.h>

enum class AssetType {
  Unknown,
  Model,
  Texture,
  Scene,
  Script
};

struct AssetRecord {
  uuids::uuid uuid;
  AssetType type = AssetType::Unknown;
  std::string path;
  std::string className; // scripts only
};

// The data half of the old AssetManager: the uuid<->path registry + serialize. It carries asset
// METADATA only (no GPU resources, no GUI). ECS3DRender's GpuAssetCache resolves these records to
// vke::Model/Texture2D; the editor's AssetBrowserPanel renders the listing.
class AssetRegistry {
public:
  void registerAsset(const AssetRecord& record);

  [[nodiscard]] const AssetRecord* getByUUID(const uuids::uuid& uuid) const;

  [[nodiscard]] const AssetRecord* getByPath(const std::string& path) const;

  [[nodiscard]] const std::unordered_map<uuids::uuid, AssetRecord>& getAssets() const;

  [[nodiscard]] nlohmann::json serialize() const;

  void loadFromJSON(const nlohmann::json& assetsData);

private:
  std::unordered_map<uuids::uuid, AssetRecord> m_assets;

  std::unordered_map<std::string, uuids::uuid> m_loadedPaths;
};



#endif //ASSETREGISTRY_H
