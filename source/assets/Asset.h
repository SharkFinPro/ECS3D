#ifndef ASSET_H
#define ASSET_H

#include <string>

constexpr int MAX_CHARACTERS = 30;

class AssetManager;

class Asset {
public:
  explicit Asset(std::string name = "Asset");
  virtual ~Asset() = default;

  void setManager(AssetManager* assetManager);

  virtual void displayGui();

protected:
  AssetManager* assetManager;

  std::string name;
};



#endif //ASSET_H
