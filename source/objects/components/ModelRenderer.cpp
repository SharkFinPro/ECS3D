#include "ModelRenderer.h"

#include "Transform.h"
#include "../Object.h"
#include "../ObjectManager.h"
#include "../../ECS3D.h"

ModelRenderer::ModelRenderer(const std::shared_ptr<VulkanEngine>& renderer, const std::shared_ptr<Texture>& texture,
                             const std::shared_ptr<Texture>& specularMap, const std::shared_ptr<Model>& model)
  : Component(ComponentType::modelRenderer), renderObject(renderer->loadRenderObject(texture, specularMap, model)),
    shouldRender(true)
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
