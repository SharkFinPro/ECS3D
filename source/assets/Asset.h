#ifndef ASSET_H
#define ASSET_H

#include <nlohmann/json_fwd.hpp>
#include <string>

class AssetManager;

class Asset {
public:
  explicit Asset(std::string name = "Asset");
  virtual ~Asset() = default;

  void setManager(AssetManager* assetManager);

  [[nodiscard]] std::string getName() const;

  virtual void displayGui();

  virtual void load() = 0;

  [[nodiscard]] virtual nlohmann::json serialize() = 0;

protected:
  AssetManager* m_assetManager = nullptr;

  std::string m_name;
};



#endif //ASSET_H
