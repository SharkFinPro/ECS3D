#include "ModelRenderer.h"
#include "Transform.h"
#include "../Object.h"
#include "../ObjectManager.h"
#include "../../ECS3D.h"
#include "../../assets/Asset.h"
#include "../../assets/TextureAsset.h"
#include "../../assets/ModelAsset.h"
#include <VulkanEngine/components/assets/AssetManager.h>
#include <VulkanEngine/components/pipelines/implementations/common/PipelineTypes.h>
#include <VulkanEngine/components/renderingManager/RenderingManager.h>
#include <VulkanEngine/components/renderingManager/renderer3D/Renderer3D.h>

ModelRenderer::ModelRenderer(const std::shared_ptr<vke::VulkanEngine>& renderer, const std::shared_ptr<vke::Texture2D>& texture,
                             const std::shared_ptr<vke::Texture2D>& specularMap, const std::shared_ptr<vke::Model>& model)
  : Component(ComponentType::modelRenderer), renderObject(renderer->getAssetManager()->loadRenderObject(texture, specularMap, model)),
    renderer(renderer), texture(texture), specularMap(specularMap), model(model), shouldRender(true)
{}

ModelRenderer::ModelRenderer(const std::shared_ptr<vke::VulkanEngine> &renderer)
  : Component(ComponentType::modelRenderer), renderObject(nullptr), renderer(renderer), texture(nullptr),
    specularMap(nullptr), model(nullptr), shouldRender(false)
{}

void ModelRenderer::variableUpdate([[maybe_unused]] const float dt)
{
  if (!canRender())
  {
    shouldRender = false;
  }

  if (!shouldRender)
  {
    return;
  }

  if (transform_ptr.expired())
  {
    transform_ptr = owner->getComponent<Transform>(ComponentType::transform);

    if (transform_ptr.expired())
    {
      return;
    }
  }

  if (const std::shared_ptr<Transform> transform = transform_ptr.lock())
  {
    renderObject->setPosition(transform->getPosition());
    renderObject->setScale(transform->getScale());
    renderObject->setOrientationEuler(transform->getRotation());
  }

  owner->getManager()->getECS()->getRenderer()->getRenderingManager()->getRenderer3D()->renderObject(
    renderObject,
    useStandardPipeline ? vke::PipelineType::object : vke::PipelineType::ellipticalDots,
    &m_selectedByRenderer
  );
}

void ModelRenderer::displayGui()
{
  if (displayGuiHeader())
  {
    ImGui::Checkbox("Use Standard Pipeline", &useStandardPipeline);
    ImGui::Checkbox("Render", &shouldRender);

    displayTextureDragDrop();
    displaySpecularDragDrop();
    displayModelDragDrop();
  }
}

bool ModelRenderer::selectedByRenderer() const
{
  return m_selectedByRenderer;
}

void ModelRenderer::renderHighlight() const
{
  if (!shouldRender)
  {
    return;
  }

  getOwner()->getManager()->getECS()->getRenderer()->getRenderingManager()->getRenderer3D()->renderObject(renderObject, vke::PipelineType::objectHighlight);
}

void ModelRenderer::displayDragDrop(const char* label,
                                    const std::function<bool(const std::shared_ptr<Asset>&)>& setter)
{
  constexpr int widgetHeight = 50;

  ImGui::BeginChild(label, {ImGui::GetContentRegionAvail().x, widgetHeight});
  ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, {0.0, 0.5});
  ImGui::Button(label, {ImGui::GetContentRegionAvail().x, widgetHeight});
  ImGui::PopStyleVar();

  if (ImGui::BeginDragDropTarget())
  {
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("asset"))
    {
      if (setter(*static_cast<std::shared_ptr<Asset>*>(payload->Data)) && canRender())
      {
        renderObject.reset();
        renderObject = renderer->getAssetManager()->loadRenderObject(texture, specularMap, model);
      }
    }

    ImGui::EndDragDropTarget();
  }

  ImGui::EndChild();
}

void ModelRenderer::displayTextureDragDrop()
{
  displayDragDrop("Texture", [this](const std::shared_ptr<Asset>& asset) -> bool
  {
    if (const auto textureAsset = std::dynamic_pointer_cast<TextureAsset>(asset))
    {
      texture = textureAsset->getTexture();

      return true;
    }

    return false;
  });
}

void ModelRenderer::displaySpecularDragDrop()
{
  displayDragDrop("Specular Map", [this](const std::shared_ptr<Asset>& asset) -> bool
  {
    if (const auto textureAsset = std::dynamic_pointer_cast<TextureAsset>(asset))
    {
      specularMap = textureAsset->getTexture();

      return true;
    }

    return false;
  });
}

void ModelRenderer::displayModelDragDrop()
{
  displayDragDrop("Model", [this](const std::shared_ptr<Asset>& asset) -> bool
  {
    if (const auto modelAsset = std::dynamic_pointer_cast<ModelAsset>(asset))
    {
      model = modelAsset->getModel();

      return true;
    }

    return false;
  });
}

bool ModelRenderer::canRender() const
{
  return texture && specularMap && model;
}
