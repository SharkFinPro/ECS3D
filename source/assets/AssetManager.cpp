#include "AssetManager.h"
#include "Asset.h"

AssetManager::AssetManager(const std::vector<std::shared_ptr<Asset>>& assets)
{
  for (const auto& asset : assets)
  {
    loadAsset(asset);
  }
}

void AssetManager::loadAsset(std::shared_ptr<Asset> asset)
{
  asset->setManager(this);
  assets.push_back(asset);
}
