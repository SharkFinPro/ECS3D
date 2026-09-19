#ifndef ASSETWIRETYPE_H
#define ASSETWIRETYPE_H

#include "assets/AssetRegistry.h"
#include <string>

// The addAsset blob's "assetType" strings, both directions. Internal to edits/: there is no buildAddAsset
// in Replication.h to borrow the mapping from - the editor assembles that blob itself (see EditorApp.cpp's
// addAsset/updatePrefabBody lambdas) and applyAddAsset reads it back.
namespace edits {

[[nodiscard]] inline std::string assetTypeToWireString(const AssetType type)
{
  switch (type)
  {
    case AssetType::Model: return "model";
    case AssetType::Texture: return "texture";
    case AssetType::Script: return "script";
    case AssetType::Prefab: return "prefab";
    case AssetType::Scene: return "scene";
    default: return "";
  }
}

[[nodiscard]] inline AssetType assetTypeFromWireString(const std::string& type)
{
  if (type == "model")
  {
    return AssetType::Model;
  }

  if (type == "texture")
  {
    return AssetType::Texture;
  }

  if (type == "script")
  {
    return AssetType::Script;
  }

  if (type == "prefab")
  {
    return AssetType::Prefab;
  }

  if (type == "scene")
  {
    return AssetType::Scene;
  }

  return AssetType::Unknown;
}

}

#endif //ASSETWIRETYPE_H
