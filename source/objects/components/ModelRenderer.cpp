#include "ModelRenderer.h"

#include "Transform.h"
#include "../Object.h"

ModelRenderer::ModelRenderer(const std::shared_ptr<VulkanEngine>& renderer, const std::shared_ptr<Texture>& texture,
                             const std::shared_ptr<Texture>& specularMap, const std::shared_ptr<Model>& model)
  : Component(ComponentType::modelRenderer), renderObject(renderer->loadRenderObject(texture, specularMap, model))
{}

void ModelRenderer::variableUpdate([[maybe_unused]] const float dt)
{
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
    renderObject->setRotation(transform->getRotation());
  }
}

void ModelRenderer::enableRendering() const
{
  renderObject->enableRendering();
}

void ModelRenderer::disableRendering() const
{
  renderObject->disableRendering();
}
