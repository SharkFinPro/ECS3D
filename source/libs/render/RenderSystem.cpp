#include "RenderSystem.h"

void RenderSystem::variableUpdate(const std::shared_ptr<SceneAsset>& scene, RenderContext& context)
{
  // TODO: walk every renderable object in the scene and submit it to the renderer obtained from
  // TODO:   the RenderContext, replacing the getOwner()->...->getRenderer3D()->renderObject chain.
  (void)scene;
  (void)context;
}
