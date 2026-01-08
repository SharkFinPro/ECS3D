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
  : Component(ComponentType::modelRenderer), m_renderObject(renderer->getAssetManager()->loadRenderObject(texture, specularMap, model)),
    m_renderer(renderer), m_texture(texture), m_specularMap(specularMap), m_model(model), m_shouldRender(true)
{}

ModelRenderer::ModelRenderer(const std::shared_ptr<vke::VulkanEngine> &renderer)
  : Component(ComponentType::modelRenderer), m_renderer(renderer)
{}

void ModelRenderer::variableUpdate([[maybe_unused]] const float dt)
{
  if (!canRender())
  {
    m_shouldRender = false;
  }

  if (!m_shouldRender)
  {
    return;
  }

  if (m_transform_ptr.expired())
  {
    m_transform_ptr = m_owner->getComponent<Transform>(ComponentType::transform);

    if (m_transform_ptr.expired())
    {
      return;
    }
  }

  if (const std::shared_ptr<Transform> transform = m_transform_ptr.lock())
  {
    m_renderObject->setPosition(transform->getPosition());
    m_renderObject->setScale(transform->getScale());
    m_renderObject->setOrientationEuler(transform->getRotation());
  }

  m_owner->getManager()->getECS()->getRenderer()->getRenderingManager()->getRenderer3D()->renderObject(
    m_renderObject,
    m_useStandardPipeline ? vke::PipelineType::object : vke::PipelineType::ellipticalDots,
    &m_selectedByRenderer
  );
}

void ModelRenderer::displayGui()
{
  if (displayGuiHeader())
  {
    ImGui::Checkbox("Use Standard Pipeline", &m_useStandardPipeline);
    ImGui::Checkbox("Render", &m_shouldRender);

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
  if (!m_shouldRender)
  {
    return;
  }

  getOwner()->getManager()->getECS()->getRenderer()->getRenderingManager()->getRenderer3D()->renderObject(m_renderObject, vke::PipelineType::objectHighlight);
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
        m_renderObject.reset();
        m_renderObject = m_renderer->getAssetManager()->loadRenderObject(m_texture, m_specularMap, m_model);
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
      m_texture = textureAsset->getTexture();

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
      m_specularMap = textureAsset->getTexture();

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
      m_model = modelAsset->getModel();

      return true;
    }

    return false;
  });
}

bool ModelRenderer::canRender() const
{
  return m_texture && m_specularMap && m_model;
}
