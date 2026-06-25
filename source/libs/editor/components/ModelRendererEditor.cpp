#include "ModelRendererEditor.h"
#include "../ComponentEditor.h"
#include <objects/components/ModelRenderer.h>
#include <imgui.h>
#include <memory>

void registerModelRendererEditor(ComponentEditor& componentEditor)
{
  componentEditor.registerHandler("Model Renderer", [](const std::shared_ptr<Component>& component) -> bool {
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

      if (ImGui::Checkbox("Use Standard Pipeline", &useStandardPipeline))
      {
        modelRenderer->setUseStandardPipeline(useStandardPipeline);
        edited = true;
      }

      if (ImGui::Checkbox("Render", &shouldRender))
      {
        modelRenderer->setShouldRender(shouldRender);
        edited = true;
      }

      // The data holds only asset UUIDs now; show them read-only for reference.
      ImGui::TextDisabled("Model:    %s", to_string(modelRenderer->getModelUUID()).c_str());
      ImGui::TextDisabled("Texture:  %s", to_string(modelRenderer->getTextureUUID()).c_str());
      ImGui::TextDisabled("Specular: %s", to_string(modelRenderer->getSpecularMapUUID()).c_str());

      // TODO: the Model/Texture/Specular asset drag-drop targets (setModelUUID/setTextureUUID/
      // TODO:   setSpecularMapUUID from a uuid dragged out of the AssetBrowserPanel). Needs the asset
      // TODO:   browser's drag-drop sources migrated first.
    }

    return edited;
  });
}
