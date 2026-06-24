#ifndef RENDERSYSTEM_H
#define RENDERSYSTEM_H

class ObjectManager;
class GpuAssetCache;

class RenderSystem {
public:
  void variableUpdate(ObjectManager& objectManager, GpuAssetCache& assetCache);

private:
  // The dispatch lifted out of ModelRenderer::variableUpdate / LightRenderer::variableUpdate. The
  // client/editor run this against their replicated scene view; the server never links it.
};



#endif //RENDERSYSTEM_H
