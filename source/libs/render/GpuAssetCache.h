#ifndef GPUASSETCACHE_H
#define GPUASSETCACHE_H

#include <memory>
#include <string>
#include <unordered_map>
#include <uuid.h>

namespace vke {
  class VulkanEngine;
  class Model;
  class Texture2D;
  class RenderObject;
}

class AssetRegistry;

// The GPU-load half of the old AssetManager (the Asset::load() side of ModelAsset/TextureAsset).
// Resolves the AssetRegistry's uuid->path records into vke resources, and caches the assembled
// RenderObjects the RenderSystem draws.
class GpuAssetCache {
public:
  GpuAssetCache(std::shared_ptr<vke::VulkanEngine> renderer, const AssetRegistry* assetRegistry);

  [[nodiscard]] std::shared_ptr<vke::VulkanEngine> getRenderer() const;

  std::shared_ptr<vke::Model> getModel(const uuids::uuid& uuid);

  std::shared_ptr<vke::Texture2D> getTexture(const uuids::uuid& uuid);

  std::shared_ptr<vke::RenderObject> getRenderObject(const uuids::uuid& modelUUID,
                                                     const uuids::uuid& textureUUID,
                                                     const uuids::uuid& specularMapUUID);

private:
  std::shared_ptr<vke::VulkanEngine> m_renderer;

  const AssetRegistry* m_assetRegistry;

  std::unordered_map<uuids::uuid, std::shared_ptr<vke::Model>> m_models;
  std::unordered_map<uuids::uuid, std::shared_ptr<vke::Texture2D>> m_textures;
  std::unordered_map<std::string, std::shared_ptr<vke::RenderObject>> m_renderObjects;
};



#endif //GPUASSETCACHE_H
