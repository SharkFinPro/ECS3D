#ifndef ASSET_H
#define ASSET_H

#include <nlohmann/json_fwd.hpp>
#include <uuid.h>
#include <string>

class AssetManager;

class Asset {
public:
  explicit Asset(uuids::uuid uuid,
                 std::string name);

  virtual ~Asset() = default;

  void setManager(AssetManager* assetManager);

  [[nodiscard]] std::string getName() const;

  virtual void displayGui();

  virtual void load() = 0;

  [[nodiscard]] virtual nlohmann::json serialize() = 0;

protected:
  AssetManager* m_assetManager = nullptr;

  uuids::uuid m_uuid;

  std::string m_name;
};



#endif //ASSET_H
