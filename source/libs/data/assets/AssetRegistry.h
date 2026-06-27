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

// uuid<->path registry + serialize. Carries asset metadata only (no GPU resources, no GUI).
// ECS3DRender's GpuAssetCache resolves these records to vke resources; the editor's
// AssetBrowserPanel renders the listing.
class AssetRegistry {
public:
  void registerAsset(const AssetRecord& record);

  // Drop all records (used when (re)loading a project / applying a fresh snapshot).
  void clear();

  [[nodiscard]] const AssetRecord* getByUUID(const uuids::uuid& uuid) const;

  [[nodiscard]] const AssetRecord* getByPath(const std::string& path) const;

  [[nodiscard]] const std::unordered_map<uuids::uuid, AssetRecord>& getAssets() const;

  // Monotonically incremented by registerAsset and clear. Consumers can cache
  // derived views and invalidate when this value changes.
  [[nodiscard]] size_t getVersion() const;

  [[nodiscard]] nlohmann::json serialize() const;

  void loadFromJSON(const nlohmann::json& assetsData);

private:
  std::unordered_map<uuids::uuid, AssetRecord> m_assets;

  std::unordered_map<std::string, uuids::uuid> m_loadedPaths;

  size_t m_version = 0;
};



#endif //ASSETREGISTRY_H
