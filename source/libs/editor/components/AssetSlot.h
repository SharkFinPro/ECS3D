#ifndef ASSETSLOT_H
#define ASSETSLOT_H

#include "../GuiComponents.h"
#include <imgui.h>
#include <memory>
#include <uuid.h>

class GpuAssetCache;
class AssetRegistry;

// What an asset reference slot shows and accepts.
struct AssetSlotStyle {
  const char* id;
  const char* kind;
  const char* payloadId;
  bool isTexture;
  gc::SecIcon icon;
  ImVec4 iconColor;
};

// An asset reference slot: shows the assigned asset's thumbnail or icon and filename, and accepts an asset
// of style.payloadId dropped onto it. Returns true (and fills outUUID) when something is dropped this
// frame. cache and registry may be null, in which case the slot shows the type icon.
bool drawAssetSlot(const std::shared_ptr<GpuAssetCache>& cache, const AssetRegistry* registry,
                   const AssetSlotStyle& style, const uuids::uuid& current, uuids::uuid& outUUID);

#endif //ASSETSLOT_H
