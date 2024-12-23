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

void ModelRenderer::variableUpdate([[maybe_unused]] const float dt)
{
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
  shouldRender = true;
}

void ModelRenderer::displayTextureDragDrop()
{
  ImGui::BeginChild("Texture", {ImGui::GetContentRegionAvail().x, 30});
  ImGui::Selectable("Texture");

  if (ImGui::BeginDragDropTarget())
  {
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("asset"))
    {
      const auto asset = *static_cast<std::shared_ptr<Asset>*>(payload->Data);

      if (const auto textureAsset = std::dynamic_pointer_cast<TextureAsset>(asset))
      {
        texture = textureAsset->getTexture();

        renderObject.reset();
        renderObject = renderer->loadRenderObject(texture, specularMap, model);
      }
    }

    ImGui::EndDragDropTarget();
  }

  ImGui::EndChild();
}

void ModelRenderer::displaySpecularDragDrop()
{
  ImGui::BeginChild("Specular Map", {ImGui::GetContentRegionAvail().x, 30});
  ImGui::Selectable("Specular Map");

  if (ImGui::BeginDragDropTarget())
  {
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("asset"))
    {
      const auto asset = *static_cast<std::shared_ptr<Asset>*>(payload->Data);

      if (const auto textureAsset = std::dynamic_pointer_cast<TextureAsset>(asset))
      {
        specularMap = textureAsset->getTexture();

        renderObject.reset();
        renderObject = renderer->loadRenderObject(texture, specularMap, model);
      }
    }

    ImGui::EndDragDropTarget();
  }

  ImGui::EndChild();
}

void ModelRenderer::displayModelDragDrop()
{
  ImGui::BeginChild("Model", {ImGui::GetContentRegionAvail().x, 30});
  ImGui::Selectable("Model");

  if (ImGui::BeginDragDropTarget())
  {
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("asset"))
    {
      const auto asset = *static_cast<std::shared_ptr<Asset>*>(payload->Data);

      if (const auto modelAsset = std::dynamic_pointer_cast<ModelAsset>(asset))
      {
        model = modelAsset->getModel();

        renderObject.reset();
        renderObject = renderer->loadRenderObject(texture, specularMap, model);
      }
    }

    ImGui::EndDragDropTarget();
  }

  ImGui::EndChild();
}
