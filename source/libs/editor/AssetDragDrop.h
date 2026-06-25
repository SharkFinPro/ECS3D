#ifndef ASSETDRAGDROP_H
#define ASSETDRAGDROP_H

#include <assets/AssetRegistry.h>

// Shared ImGui drag-drop payload ids for asset references. The payload data is the asset's uuid as a
// string. Type-specific ids let a drop target (e.g. the ModelRenderer's model slot) accept only the
// right asset kind without needing the AssetRegistry to validate it.
namespace assetDragDrop {
  inline constexpr const char* model = "asset_model";
  inline constexpr const char* texture = "asset_texture";
  inline constexpr const char* script = "asset_script";

  [[nodiscard]] inline const char* payloadId(const AssetType type)
  {
    switch (type)
    {
      case AssetType::Model: return model;
      case AssetType::Texture: return texture;
      case AssetType::Script: return script;
      default: return nullptr;
    }
  }
}

#endif //ASSETDRAGDROP_H
