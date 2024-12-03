#include "ModelRenderer.h"

#include "../Object.h"
#include "../ObjectManager.h"
#include "../../ECS3D.h"

ModelRenderer::ModelRenderer()
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
