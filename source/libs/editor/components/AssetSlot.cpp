#include "AssetSlot.h"
#include "../AssetDisplay.h"
#include "../AssetDragDrop.h"
#include <assets/AssetRegistry.h>
#include <GpuAssetCache.h>
#include <VulkanEngine/components/assets/textures/Texture2D.h>
#include <exception>
#include <string>

bool drawAssetSlot(const std::shared_ptr<GpuAssetCache>& cache, const AssetRegistry* registry,
                   const AssetSlotStyle& style, const uuids::uuid& current, uuids::uuid& outUUID, const bool mixed)
{
  std::string name = mixed ? "Mixed" : "None";
  ImTextureID thumb = 0;

  if (registry && !mixed)
  {
    if (const auto* record = assetDragDrop::slotRecord(*registry, style.payloadId, current))
    {
      name = assetDisplay::name(*record);

      if (style.isTexture && cache)
      {
        try
        {
          if (const auto texture = cache->getTexture(current))
          {
            thumb = texture->getImGuiTexture();
          }
        }
        catch (const std::exception&)
        {
          // fall back to the type icon
        }
      }
    }
  }

  gc::assetRefRow(style.id, style.kind, name.c_str(), thumb, style.icon, style.iconColor);

  bool dropped = false;
  if (ImGui::BeginDragDropTarget())
  {
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(style.payloadId))
    {
      const std::string uuidStr(static_cast<const char*>(payload->Data), payload->DataSize);
      if (const auto parsed = uuids::uuid::from_string(uuidStr))
      {
        outUUID = parsed.value();
        dropped = true;
      }
    }

    ImGui::EndDragDropTarget();
  }

  return dropped;
}
