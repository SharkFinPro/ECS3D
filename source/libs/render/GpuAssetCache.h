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

// Resolves AssetRegistry uuid->path records into vke resources and caches per-owner RenderObjects
// for the RenderSystem.
class GpuAssetCache {
public:
  GpuAssetCache(std::shared_ptr<vke::VulkanEngine> renderer, const AssetRegistry* assetRegistry);

  [[nodiscard]] std::shared_ptr<vke::VulkanEngine> getRenderer() const;

  std::shared_ptr<vke::Model> getModel(const uuids::uuid& uuid);

  std::shared_ptr<vke::Texture2D> getTexture(const uuids::uuid& uuid);

  // Keyed by owning object, not asset triple: two objects sharing a model get distinct render
  // objects. Rebuilds when the owner's model/texture/specular UUIDs change.
  std::shared_ptr<vke::RenderObject> getRenderObject(const uuids::uuid& ownerUUID,
                                                     const uuids::uuid& modelUUID,
                                                     const uuids::uuid& textureUUID,
                                                     const uuids::uuid& specularMapUUID);

  // A debug render object for a collider's shape (a path-loaded cube/sphere, white, no specular), keyed
  // per collider OWNER and rebuilt if the model path changes. The RenderSystem draws it with the
  // objectHighlight pipeline when the collider's render flag is on.
  std::shared_ptr<vke::RenderObject> getColliderGizmo(const uuids::uuid& ownerUUID, const std::string& modelPath);

private:
  struct CachedRenderObject {
    std::shared_ptr<vke::RenderObject> renderObject;
    uuids::uuid modelUUID;
    uuids::uuid textureUUID;
    uuids::uuid specularMapUUID;
  };

  struct CachedGizmo {
    std::shared_ptr<vke::RenderObject> renderObject;
    std::string path;
  };

  std::shared_ptr<vke::VulkanEngine> m_renderer;

  const AssetRegistry* m_assetRegistry;

  std::unordered_map<uuids::uuid, std::shared_ptr<vke::Model>> m_models;
  std::unordered_map<uuids::uuid, std::shared_ptr<vke::Texture2D>> m_textures;
  std::unordered_map<uuids::uuid, CachedRenderObject> m_renderObjects;
  std::unordered_map<uuids::uuid, CachedGizmo> m_colliderGizmos;
};



#endif //GPUASSETCACHE_H
