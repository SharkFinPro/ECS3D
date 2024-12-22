#include "Asset.h"

Asset::Asset()
  : assetManager(nullptr)
{}

void Asset::setManager(AssetManager* assetManager)
{
  this->assetManager = assetManager;
}
