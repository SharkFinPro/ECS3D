#include "ModelRendererEditor.h"
#include "../ComponentEditor.h"
#include "../AssetDragDrop.h"
#include <objects/components/ModelRenderer.h>
#include <imgui.h>
#include <memory>
#include <string>

namespace {
  // A labeled drop target for an asset uuid. Returns true (and fills outUUID) if an asset of the given
  // payload type was dropped onto it this frame.
  bool assetDropTarget(const char* label, const char* payloadId, uuids::uuid& outUUID)
  {
    bool dropped = false;

    constexpr int widgetHeight = 50;

    ImGui::BeginChild(label, {ImGui::GetContentRegionAvail().x, widgetHeight});
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, {0.0, 0.5});
    ImGui::Button(label, {ImGui::GetContentRegionAvail().x, widgetHeight});
    ImGui::PopStyleVar();

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

    ImGui::EndChild();

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

      if (assetDropTarget(std::string("Model: " + to_string(modelRenderer->getModelUUID())).c_str(), assetDragDrop::model, dropped))
      {
        modelRenderer->setModelUUID(dropped);
        edited = true;
      }

      if (assetDropTarget(std::string("Texture: " + to_string(modelRenderer->getTextureUUID())).c_str(), assetDragDrop::texture, dropped))
      {
        modelRenderer->setTextureUUID(dropped);
        edited = true;
      }

      if (assetDropTarget(std::string("Specular Map: " + to_string(modelRenderer->getSpecularMapUUID())).c_str(), assetDragDrop::texture, dropped))
      {
        modelRenderer->setSpecularMapUUID(dropped);
        edited = true;
      }
    }

    return edited;
  });
}
