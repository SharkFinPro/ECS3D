#ifndef ASSET_H
#define ASSET_H

class AssetManager;

class Asset {
public:
  Asset();

  void setManager(AssetManager* assetManager);

protected:
  AssetManager* assetManager;
};



#endif //ASSET_H
