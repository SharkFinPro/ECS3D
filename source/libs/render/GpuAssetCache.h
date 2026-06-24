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

  // Keyed by the OWNING object, not the asset triple: the old ModelRenderer held its own
  // vke::RenderObject, so two objects sharing a model still get distinct render objects. Rebuilds
  // only when the owner's model/texture/specular UUIDs change. Models/textures themselves are still
  // shared (cached by asset uuid).
  std::shared_ptr<vke::RenderObject> getRenderObject(const uuids::uuid& ownerUUID,
                                                     const uuids::uuid& modelUUID,
                                                     const uuids::uuid& textureUUID,
                                                     const uuids::uuid& specularMapUUID);

private:
  struct CachedRenderObject {
    std::shared_ptr<vke::RenderObject> renderObject;
    uuids::uuid modelUUID;
    uuids::uuid textureUUID;
    uuids::uuid specularMapUUID;
  };

  std::shared_ptr<vke::VulkanEngine> m_renderer;

  const AssetRegistry* m_assetRegistry;

  std::unordered_map<uuids::uuid, std::shared_ptr<vke::Model>> m_models;
  std::unordered_map<uuids::uuid, std::shared_ptr<vke::Texture2D>> m_textures;
  std::unordered_map<uuids::uuid, CachedRenderObject> m_renderObjects;
};



#endif //GPUASSETCACHE_H
