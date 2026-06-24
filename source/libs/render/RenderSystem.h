#ifndef RENDERSYSTEM_H
#define RENDERSYSTEM_H

#include <memory>

class SceneAsset;
class RenderContext;

class RenderSystem {
public:
  void variableUpdate(const std::shared_ptr<SceneAsset>& scene, RenderContext& context);

private:
  // TODO: migrate ModelRenderer::variableUpdate (renderObject) and LightRenderer (PointLight/
  // TODO:   SpotLight submission). The client/editor run this against its replicated scene view.
};



#endif //RENDERSYSTEM_H
