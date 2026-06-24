#ifndef GPUASSETCACHE_H
#define GPUASSETCACHE_H

class GpuAssetCache {
public:
  // TODO: own the GPU-side of assets, split out of AssetManager: trigger Asset::load() (model
  // TODO:   meshes, textures) and hand out the loaded handles the RenderSystem draws with. Keyed
  // TODO:   by the same uuids the ECS3DData AssetRegistry uses.
};



#endif //GPUASSETCACHE_H
