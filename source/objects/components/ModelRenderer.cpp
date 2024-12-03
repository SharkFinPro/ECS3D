#include "ModelRenderer.h"

#include "Transform.h"
#include "../Object.h"

ModelRenderer::ModelRenderer(const std::shared_ptr<VulkanEngine>& renderer, const char* texturePath,
                             const char* specularMapPath, const char* modelPath)
  : Component(ComponentType::modelRenderer)
{
  const std::shared_ptr<Texture> texture = renderer->loadTexture(texturePath);
  const std::shared_ptr<Texture> specularMap = renderer->loadTexture(specularMapPath);
  const std::shared_ptr<Model> model = renderer->loadModel(modelPath);

  renderObject = renderer->loadRenderObject(texture, specularMap, model);
}

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
  }
}