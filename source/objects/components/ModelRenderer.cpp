#include "ModelRenderer.h"

ModelRenderer::ModelRenderer(const std::shared_ptr<VulkanEngine>& renderer)
  : Component(ComponentType::modelRenderer)
{
  const std::shared_ptr<Texture> texture = renderer->loadTexture("");
  const std::shared_ptr<Texture> specularMap = renderer->loadTexture("");
  const std::shared_ptr<Model> model = renderer->loadModel("");

  renderObject = renderer->loadRenderObject(texture, specularMap, model);
}

void ModelRenderer::variableUpdate([[maybe_unused]] const float dt)
{

}
