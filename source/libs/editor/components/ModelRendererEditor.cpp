#include "ModelRendererEditor.h"
#include "AssetSlot.h"
#include "../ComponentEditor.h"
#include "../AssetDragDrop.h"
#include "../GuiComponents.h"
#include "../MixedFields.h"
#include <objects/components/ModelRenderer.h>
#include <imgui.h>
#include <memory>
#include <utility>

void registerModelRendererEditor(ComponentEditor& componentEditor,
                                 std::shared_ptr<GpuAssetCache> assetCache,
                                 const AssetRegistry* assetRegistry)
{
  componentEditor.registerHandler("Model Renderer",
    [cache = std::move(assetCache), registry = assetRegistry](const std::shared_ptr<Component>& component,
                                                               const MixedFields& mixed) -> bool {
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

      if (gc::accentCheckbox("Use Standard Pipeline", &useStandardPipeline, mixed.contains("useStandardPipeline")))
      {
        modelRenderer->setUseStandardPipeline(useStandardPipeline);
        edited = true;
      }

      if (gc::accentCheckbox("Render", &shouldRender, mixed.contains("shouldRender")))
      {
        modelRenderer->setShouldRender(shouldRender);
        edited = true;
      }

      float reflectivity = modelRenderer->getReflectivity();
      if (gc::accentSlider("Reflectivity", &reflectivity, 0.0f, 1.0f, mixed.contains("reflectivity")))
      {
        modelRenderer->setReflectivity(reflectivity);
        edited = true;
      }

      ImGui::Spacing();

      // Drag an asset out of the AssetBrowserPanel onto these slots to assign it.
      uuids::uuid dropped;

      const AssetSlotStyle modelSlot{ "modelSlot", "Model", assetDragDrop::model, false,
                                      gc::SecIcon::model, theme::modelPurple };
      if (drawAssetSlot(cache, registry, modelSlot, modelRenderer->getModelUUID(), dropped))
      {
        modelRenderer->setModelUUID(dropped);
        edited = true;
      }

      const AssetSlotStyle textureSlot{ "textureSlot", "Texture", assetDragDrop::texture, true,
                                        gc::SecIcon::image, theme::accent };
      if (drawAssetSlot(cache, registry, textureSlot, modelRenderer->getTextureUUID(), dropped))
      {
        modelRenderer->setTextureUUID(dropped);
        edited = true;
      }

      const AssetSlotStyle specularSlot{ "specularSlot", "Specular Map", assetDragDrop::texture, true,
                                         gc::SecIcon::image, theme::accent };
      if (drawAssetSlot(cache, registry, specularSlot, modelRenderer->getSpecularMapUUID(), dropped))
      {
        modelRenderer->setSpecularMapUUID(dropped);
        edited = true;
      }
    }

    return edited;
  });
}
