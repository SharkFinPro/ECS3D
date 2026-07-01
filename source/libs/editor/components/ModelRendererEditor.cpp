#include "ModelRendererEditor.h"
#include "../ComponentEditor.h"
#include "../AssetDragDrop.h"
#include "../GuiComponents.h"
#include <objects/components/ModelRenderer.h>
#include <assets/AssetRegistry.h>
#include <GpuAssetCache.h>
#include <VulkanEngine/components/assets/textures/Texture2D.h>
#include <imgui.h>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace {
  // A model-renderer asset reference slot: shows the assigned asset's thumbnail/icon + filename (the
  // mockup's Model/Texture/Specular rows) and accepts an asset of `payloadId` dropped onto it. Returns
  // true (and fills outUUID) when something is dropped this frame. cache/registry may be null.
  bool assetSlot(const std::shared_ptr<GpuAssetCache>& cache, const AssetRegistry* registry,
                 const char* id, const char* kind, const char* payloadId, const uuids::uuid& current,
                 const bool isTexture, const gc::SecIcon icon, const ImVec4& iconCol, uuids::uuid& outUUID)
  {
    std::string name = "None";
    ImTextureID thumb = 0;

    if (registry)
    {
      if (const auto* record = registry->getByUUID(current))
      {
        name = std::filesystem::path(record->path).filename().string();

        if (isTexture && cache)
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

    gc::assetRefRow(id, kind, name.c_str(), thumb, icon, iconCol);

    bool dropped = false;
    if (ImGui::BeginDragDropTarget())
    {
      if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(payloadId))
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
}

void registerModelRendererEditor(ComponentEditor& componentEditor,
                                 std::shared_ptr<GpuAssetCache> assetCache,
                                 const AssetRegistry* assetRegistry)
{
  componentEditor.registerHandler("Model Renderer",
    [cache = std::move(assetCache), registry = assetRegistry](const std::shared_ptr<Component>& component) -> bool {
    const auto modelRenderer = std::dynamic_pointer_cast<ModelRenderer>(component);
    if (!modelRenderer)
    {
      return false;
    }

    bool edited = false;

    if (ComponentEditor::displayHeader(component))
    {
      bool useStandardPipeline = modelRenderer->getUseStandardPipeline();
      bool shouldRender = modelRenderer->getShouldRender();

      if (gc::accentCheckbox("Use Standard Pipeline", &useStandardPipeline))
      {
        modelRenderer->setUseStandardPipeline(useStandardPipeline);
        edited = true;
      }

      if (gc::accentCheckbox("Render", &shouldRender))
      {
        modelRenderer->setShouldRender(shouldRender);
        edited = true;
      }

      ImGui::Spacing();

      // Drag an asset out of the AssetBrowserPanel onto these slots to assign it.
      uuids::uuid dropped;

      if (assetSlot(cache, registry, "modelSlot", "Model", assetDragDrop::model,
                    modelRenderer->getModelUUID(), false, gc::SecIcon::model, theme::modelPurple, dropped))
      {
        modelRenderer->setModelUUID(dropped);
        edited = true;
      }

      if (assetSlot(cache, registry, "textureSlot", "Texture", assetDragDrop::texture,
                    modelRenderer->getTextureUUID(), true, gc::SecIcon::image, theme::accent, dropped))
      {
        modelRenderer->setTextureUUID(dropped);
        edited = true;
      }

      if (assetSlot(cache, registry, "specularSlot", "Specular Map", assetDragDrop::texture,
                    modelRenderer->getSpecularMapUUID(), true, gc::SecIcon::image, theme::accent, dropped))
      {
        modelRenderer->setSpecularMapUUID(dropped);
        edited = true;
      }
    }

    return edited;
  });
}
