#include "ModelRenderer.h"
#include "Transform.h"
#include "../Object.h"
#include "../ObjectManager.h"
#include "../../ECS3D.h"
#include "../../assets/Asset.h"
#include "../../assets/AssetManager.h"
#include "../../assets/TextureAsset.h"
#include "../../assets/ModelAsset.h"
#include <nlohmann/json.hpp>
#include <uuid.h>
#include <VulkanEngine/components/assets/AssetManager.h>
#include <VulkanEngine/components/assets/objects/Model.h>
#include <VulkanEngine/components/pipelines/implementations/common/PipelineTypes.h>
#include <VulkanEngine/components/renderingManager/RenderingManager.h>
#include <VulkanEngine/components/renderingManager/renderer3D/Renderer3D.h>

ModelRenderer::ModelRenderer(std::shared_ptr<vke::VulkanEngine> renderer,
                             std::shared_ptr<TextureAsset> texture,
                             std::shared_ptr<TextureAsset> specularMap,
                             std::shared_ptr<ModelAsset> model)
  : Component(ComponentType::modelRenderer),
    m_renderObject(renderer->getAssetManager()->loadRenderObject(texture->getTexture(), specularMap->getTexture(), model->getModel())),
    m_renderer(std::move(renderer)), m_texture(std::move(texture)), m_specularMap(std::move(specularMap)),
    m_model(std::move(model)), m_shouldRender(true)
{}

ModelRenderer::ModelRenderer(std::shared_ptr<vke::VulkanEngine> renderer)
  : Component(ComponentType::modelRenderer), m_renderer(std::move(renderer))
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

    if (!canRender())
    {
      m_shouldRender = false;
    }
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

nlohmann::json ModelRenderer::serialize()
{
  const nlohmann::json data = {
    { "type", "ModelRenderer" },
    { "shouldRender", m_shouldRender },
    { "modelUUID", m_model ? uuids::to_string(m_model->getUUID()) : "" },
    { "textureUUID", m_texture ? uuids::to_string(m_texture->getUUID()) : "" },
    { "specularMapUUID", m_specularMap ? uuids::to_string(m_specularMap->getUUID()) : "" }
  };

  return data;
}

void ModelRenderer::loadFromJSON(const nlohmann::json& componentData)
{
  m_shouldRender = componentData.at("shouldRender");

  const auto modelUUID = uuids::uuid::from_string(std::string(componentData.at("modelUUID")));
  const auto textureUUID = uuids::uuid::from_string(std::string(componentData.at("textureUUID")));
  const auto specularMapUUID = uuids::uuid::from_string(std::string(componentData.at("specularMapUUID")));

  if (modelUUID.has_value())
  {
    if (const auto modelAsset = m_owner->getManager()->getECS()->getAssetManager()->getAsset<ModelAsset>(modelUUID.value()))
    {
      m_model = modelAsset;
    }
  }

  if (textureUUID.has_value())
  {
    if (const auto textureAsset = m_owner->getManager()->getECS()->getAssetManager()->getAsset<TextureAsset>(textureUUID.value()))
    {
      m_texture = textureAsset;
    }
  }

  if (specularMapUUID.has_value())
  {
    if (const auto specularMapAsset = m_owner->getManager()->getECS()->getAssetManager()->getAsset<TextureAsset>(specularMapUUID.value()))
    {
      m_specularMap = specularMapAsset;
    }
  }

  if (canRender())
  {
    m_renderObject.reset();
    m_renderObject = m_renderer->getAssetManager()->loadRenderObject(m_texture->getTexture(), m_specularMap->getTexture(), m_model->getModel());
  }
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
        m_renderObject = m_renderer->getAssetManager()->loadRenderObject(m_texture->getTexture(), m_specularMap->getTexture(), m_model->getModel());
      }
    }

    ImGui::EndDragDropTarget();
  }

  ImGui::EndChild();
}

void ModelRenderer::displayTextureDragDrop()
{
  const auto label = "Texture: " + std::string(m_texture ? m_texture->getName() : "");
  displayDragDrop(label.c_str(), [this](const std::shared_ptr<Asset>& asset) -> bool
  {
    if (const auto textureAsset = std::dynamic_pointer_cast<TextureAsset>(asset))
    {
      m_texture = textureAsset;

      return true;
    }

    return false;
  });
}

void ModelRenderer::displaySpecularDragDrop()
{
  const auto label = "Specular Map: " + std::string(m_specularMap ? m_specularMap->getName() : "");
  displayDragDrop(label.c_str(), [this](const std::shared_ptr<Asset>& asset) -> bool
  {
    if (const auto textureAsset = std::dynamic_pointer_cast<TextureAsset>(asset))
    {
      m_specularMap = textureAsset;

      return true;
    }

    return false;
  });
}

void ModelRenderer::displayModelDragDrop()
{
  const auto label = "Model: " + std::string(m_model ? m_model->getName() : "");
  displayDragDrop(label.c_str(), [this](const std::shared_ptr<Asset>& asset) -> bool
  {
    if (const auto modelAsset = std::dynamic_pointer_cast<ModelAsset>(asset))
    {
      m_model = modelAsset;

      return true;
    }

    return false;
  });
}

bool ModelRenderer::canRender() const
{
  return m_texture && m_specularMap && m_model;
}
