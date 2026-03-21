#ifndef ASSET_H
#define ASSET_H

#include <nlohmann/json_fwd.hpp>
#include <uuid.h>
#include <string>

class AssetManager;

enum class AssetType {
  Unknown,
  Model,
  Script,
  Texture
};

const std::unordered_map<AssetType, std::string> assetTypeToString {
  { AssetType::Model, "Model" },
  { AssetType::Script, "Script" },
  { AssetType::Texture, "Texture" }
};

class Asset {
public:
  explicit Asset(AssetType assetType,
                 uuids::uuid uuid,
                 std::string name);

  virtual ~Asset() = default;

  void setManager(AssetManager* assetManager);

  [[nodiscard]] AssetType getAssetType() const;

  [[nodiscard]] std::string getName() const;

  virtual void displayGui(float cellSize);

  virtual void load() = 0;

  [[nodiscard]] uuids::uuid getUUID() const;

  [[nodiscard]] virtual nlohmann::json serialize() = 0;

protected:
  AssetManager* m_assetManager = nullptr;

  AssetType m_assetType = AssetType::Unknown;

  uuids::uuid m_uuid;

  std::string m_name;
};



#endif //ASSET_H
