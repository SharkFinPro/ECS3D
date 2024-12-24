#include "ModelRenderer.h"
#include "Transform.h"
#include "../Object.h"
#include "../ObjectManager.h"
#include "../../ECS3D.h"
#include "../../assets/Asset.h"
#include "../../assets/TextureAsset.h"
#include "../../assets/ModelAsset.h"

ModelRenderer::ModelRenderer(const std::shared_ptr<VulkanEngine>& renderer, const std::shared_ptr<Texture>& texture,
                             const std::shared_ptr<Texture>& specularMap, const std::shared_ptr<Model>& model)
  : Component(ComponentType::modelRenderer), renderObject(renderer->loadRenderObject(texture, specularMap, model)),
    renderer(renderer), texture(texture), specularMap(specularMap), model(model), shouldRender(true)
{}

ModelRenderer::ModelRenderer(const std::shared_ptr<VulkanEngine> &renderer)
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
    transform_ptr = std::dynamic_pointer_cast<Transform>(owner->getComponent(ComponentType::transform));

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

  owner->getManager()->getECS()->getRenderer()->renderObject(renderObject);
}

void ModelRenderer::displayGui()
{
  if (ImGui::CollapsingHeader("Model Renderer"))
  {
    ImGui::Checkbox("Render", &shouldRender);

    displayTextureDragDrop();
    displaySpecularDragDrop();
    displayModelDragDrop();

    if (ImGui::Button("Reset"))
    {
      reset();
    }
  }
}

void ModelRenderer::reset()
{
  shouldRender = canRender();
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
        renderObject = renderer->loadRenderObject(texture, specularMap, model);
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
