#ifndef ASSETDRAGDROP_H
#define ASSETDRAGDROP_H

#include <assets/AssetRegistry.h>
#include <optional>
#include <string_view>
#include <uuid.h>

// Shared ImGui drag-drop payload ids for asset references. The payload data is the asset's uuid as a
// string. Type-specific ids let a drop target (e.g. the ModelRenderer's model slot) accept only the
// right asset kind without needing the AssetRegistry to validate it.
namespace assetDragDrop {
  inline constexpr const char* model = "asset_model";
  inline constexpr const char* texture = "asset_texture";
  inline constexpr const char* script = "asset_script";
  inline constexpr const char* prefab = "asset_prefab";
  inline constexpr const char* scene = "asset_scene";

  [[nodiscard]] inline const char* payloadId(const AssetType type)
  {
    switch (type)
    {
      case AssetType::Model: return model;
      case AssetType::Texture: return texture;
      case AssetType::Script: return script;
      case AssetType::Prefab: return prefab;
      case AssetType::Scene: return scene;
      default: return nullptr;
    }
  }

  // The scene a dropped payload names, or nothing when the bytes (a uuid string with no terminator) do not
  // parse or the registry holds no Scene record for it. Kept free of ImGui so it can be tested headless.
  [[nodiscard]] inline std::optional<uuids::uuid> sceneFromPayload(const void* data, const int size,
                                                                   const AssetRegistry& registry)
  {
    if (!data || size <= 0)
    {
      return std::nullopt;
    }

    const std::string_view text(static_cast<const char*>(data), static_cast<size_t>(size));
    const auto uuid = uuids::uuid::from_string(text);
    if (!uuid || !registry.getByUUIDOfType(*uuid, AssetType::Scene))
    {
      return std::nullopt;
    }

    return uuid;
  }
}

#endif //ASSETDRAGDROP_H
