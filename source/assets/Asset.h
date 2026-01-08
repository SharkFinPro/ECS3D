#ifndef ASSET_H
#define ASSET_H

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

protected:
  AssetManager* m_assetManager;

  std::string m_name;
};



#endif //ASSET_H
