#include "ModelRendererEditor.h"
#include "../ComponentEditor.h"
#include "../AssetDragDrop.h"
#include <objects/components/ModelRenderer.h>
#include <imgui.h>
#include <memory>
#include <string>

namespace {
  // A labelled drop target for an asset uuid. Returns true (and fills outUUID) if an asset of the given
  // payload type was dropped onto it this frame.
  bool assetDropTarget(const char* label, const uuids::uuid& current, const char* payloadId, uuids::uuid& outUUID)
  {
    ImGui::Button(current.is_nil() ? "<none>" : uuids::to_string(current).c_str());

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

    ImGui::SameLine();
    ImGui::TextUnformatted(label);

    return dropped;
  }
}

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

      // Drag an asset out of the AssetBrowserPanel onto these slots to assign it.
      uuids::uuid dropped;

      if (assetDropTarget("Model", modelRenderer->getModelUUID(), assetDragDrop::model, dropped))
      {
        modelRenderer->setModelUUID(dropped);
        edited = true;
      }

      if (assetDropTarget("Texture", modelRenderer->getTextureUUID(), assetDragDrop::texture, dropped))
      {
        modelRenderer->setTextureUUID(dropped);
        edited = true;
      }

      if (assetDropTarget("Specular", modelRenderer->getSpecularMapUUID(), assetDragDrop::texture, dropped))
      {
        modelRenderer->setSpecularMapUUID(dropped);
        edited = true;
      }
    }

    return edited;
  });
}
