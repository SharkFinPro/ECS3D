#ifndef ASSETMANAGER_H
#define ASSETMANAGER_H

#include <vector>
#include <memory>

class Asset;

class AssetManager {
public:
  AssetManager() = default;
  explicit AssetManager(const std::vector<std::shared_ptr<Asset>>& assets);

  void loadAsset(std::shared_ptr<Asset> asset);

private:
  std::vector<std::shared_ptr<Asset>> assets;
};



#endif //ASSETMANAGER_H
