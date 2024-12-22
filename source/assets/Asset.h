#ifndef ASSET_H
#define ASSET_H

#include <string>

class AssetManager;

class Asset {
public:
  explicit Asset(std::string name = "Asset");
  virtual ~Asset() = default;

  void setManager(AssetManager* assetManager);

  virtual void displayGui();

  virtual void load() = 0;

protected:
  AssetManager* assetManager;

  std::string name;
};



#endif //ASSET_H
