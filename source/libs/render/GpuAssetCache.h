#ifndef GPUASSETCACHE_H
#define GPUASSETCACHE_H

#include "SharedAssetLoads.h"
#include <cstddef>
#include <objects/components/collisions/ConvexHull.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
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

  // Both return nullptr for an unknown uuid or an asset that fails to load (logged once per path).
  std::shared_ptr<vke::Model> getModel(const uuids::uuid& uuid);

  std::shared_ptr<vke::Texture2D> getTexture(const uuids::uuid& uuid);

  // Keyed by owning object, not asset triple: two objects sharing a model get distinct render
  // objects. Rebuilds when the owner's model/texture/specular UUIDs change.
  std::shared_ptr<vke::RenderObject> getRenderObject(const uuids::uuid& ownerUUID,
                                                     const uuids::uuid& modelUUID,
                                                     const uuids::uuid& textureUUID,
                                                     const uuids::uuid& specularMapUUID);

  // A debug render object for a collider's shape (a path-loaded cube/sphere/cylinder, white, no specular),
  // keyed per collider OWNER and part, and rebuilt if the model path changes. A shape drawn from several
  // meshes (a capsule's cylinder and two end spheres) asks for one part index per mesh; the rest use part 0.
  // The mesh and texture are loaded once and shared; returns nullptr (logged once) if either fails to load.
  // The RenderSystem outlines it when the collider's render flag is on.
  std::shared_ptr<vke::RenderObject> getColliderGizmo(const uuids::uuid& ownerUUID, const std::string& modelPath,
                                                      std::size_t part = 0);

  // Releases the owner's gizmo parts from partCount upward, and all of them for zero. Called each frame with the
  // number of parts the owner's shape drew, so a gizmo that is switched off or changes shape does not keep
  // its old render objects alive; dropping the last reference frees them, as pruneStale does.
  void trimColliderGizmo(const uuids::uuid& ownerUUID, std::size_t partCount);

  // A debug render object for a convex mesh collider's hull, built from the hull's own triangles (white, flat
  // normals), keyed per collider OWNER. It is compared with the hull it was built from on each call and rebuilt
  // (the old model released) only when the hull's content differs; comparing a few hundred vertices is cheaper
  // than adding a version counter to the collider that every hull edit and wire unpack would have to maintain.
  // Returns nullptr (logged once per hull) if the model cannot be built.
  std::shared_ptr<vke::RenderObject> getHullGizmo(const uuids::uuid& ownerUUID, const ConvexHull& hull);

  // The editor's light sprite (a double-sided unit quad textured with the light icon), keyed per light
  // OWNER since each render object carries its own transform. The RenderSystem draws it as a billboard.
  std::shared_ptr<vke::RenderObject> getLightGizmo(const uuids::uuid& ownerUUID);

  // Releases the owner's hull gizmo, if any. Called for an owner that is not drawing one this frame.
  void dropHullGizmo(const uuids::uuid& ownerUUID);

  // Drops the per-owner render object / collider gizmo / hull gizmo / light gizmo for any uuid not in
  // liveUUIDs. Called once per
  // frame from RenderSystem::variableUpdate with the set of objects it just walked, so a deleted
  // object's GPU resources are released instead of accumulating for the life of the process. Shared
  // assets (m_models/m_textures, keyed by asset uuid rather than owner) are left alone.
  void pruneStale(const std::unordered_set<uuids::uuid>& liveUUIDs);

private:
  struct CachedRenderObject {
    std::shared_ptr<vke::RenderObject> renderObject;
    uuids::uuid modelUUID;
    uuids::uuid textureUUID;
    uuids::uuid specularMapUUID;
  };

  // Loads the shared quad model and icon once. A missing or invalid file is logged once and remembered, so
  // it is not retried every frame.
  void loadLightGizmoAssets();

  struct CachedGizmo {
    std::shared_ptr<vke::RenderObject> renderObject;
    std::string path;
  };

  struct CachedHullGizmo {
    std::shared_ptr<vke::RenderObject> renderObject;
    std::shared_ptr<vke::Model> model;
    ConvexHull hull;
  };

  void releaseHullGizmo(CachedHullGizmo& cached);

  std::shared_ptr<vke::VulkanEngine> m_renderer;

  const AssetRegistry* m_assetRegistry;

  // Keyed by asset path, so a record re-imported under a new path gets a fresh attempt while a path that
  // failed is logged once and not retried.
  SharedAssetLoads<vke::Model> m_models;
  SharedAssetLoads<vke::Texture2D> m_textures;
  std::unordered_map<uuids::uuid, CachedRenderObject> m_renderObjects;
  std::unordered_map<uuids::uuid, std::vector<CachedGizmo>> m_colliderGizmos;
  std::unordered_map<uuids::uuid, CachedHullGizmo> m_hullGizmos;
  std::unordered_map<uuids::uuid, std::shared_ptr<vke::RenderObject>> m_lightGizmos;
  std::shared_ptr<vke::Model> m_lightGizmoModel;
  std::shared_ptr<vke::Texture2D> m_lightGizmoIcon;
  bool m_lightGizmoAssetsTried = false;

  SharedAssetLoads<vke::Model> m_gizmoModels;
  SharedAssetLoads<vke::Texture2D> m_gizmoTextures;
};



#endif //GPUASSETCACHE_H
