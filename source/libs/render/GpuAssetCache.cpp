#include "GpuAssetCache.h"
#include "HullGizmoMesh.h"
#include <assets/AssetRegistry.h>
#include <ProjectPaths.h>
#include <Log.h>
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/assets/AssetManager.h>
#include <VulkanEngine/components/pipelines/implementations/vertexInputs/Vertex.h>
#include <VulkanEngine/components/assets/objects/Model.h>
#include <VulkanEngine/components/assets/objects/RenderObject.h>
#include <VulkanEngine/components/assets/textures/Texture2D.h>
#include <array>
#include <cstddef>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

GpuAssetCache::GpuAssetCache(std::shared_ptr<vke::VulkanEngine> renderer, const AssetRegistry* assetRegistry)
  : m_renderer(std::move(renderer)), m_assetRegistry(assetRegistry)
{}

std::shared_ptr<vke::VulkanEngine> GpuAssetCache::getRenderer() const
{
  return m_renderer;
}

void GpuAssetCache::setProjectRoot(std::filesystem::path projectRoot)
{
  if (projectRoot != m_projectRoot)
  {
    m_projectRoot = std::move(projectRoot);
    m_resetPending = true;
  }
}

void GpuAssetCache::requestAssetReset()
{
  m_resetPending = true;
}

const std::filesystem::path& GpuAssetCache::getProjectRoot() const
{
  return m_projectRoot;
}

std::string GpuAssetCache::resolveRecordPath(const std::string& recordPath) const
{
  const auto resolved = resolveAssetPath(m_projectRoot, recordPath);
  if (!resolved)
  {
    throw std::runtime_error("asset path leaves the project folder");
  }

  return resolved->string();
}

std::shared_ptr<vke::Model> GpuAssetCache::getModel(const uuids::uuid& uuid)
{
  const auto record = m_assetRegistry->getByUUIDOfType(uuid, AssetType::Model);
  if (!record)
  {
    return nullptr;
  }

  return m_models.get(uuids::to_string(uuid), record->path + " (" + uuids::to_string(uuid) + ")", [this, &record] {
    return m_renderer->getAssetManager()->loadModel(resolveRecordPath(record->path).c_str());
  });
}

std::shared_ptr<vke::Texture2D> GpuAssetCache::getTexture(const uuids::uuid& uuid)
{
  const auto record = m_assetRegistry->getByUUIDOfType(uuid, AssetType::Texture);
  if (!record)
  {
    return nullptr;
  }

  return m_textures.get(uuids::to_string(uuid), record->path + " (" + uuids::to_string(uuid) + ")", [this, &record] {
    return m_renderer->getAssetManager()->loadTexture(resolveRecordPath(record->path).c_str());
  });
}

GpuAssetCache::GpuRenderAssets GpuAssetCache::resolveRenderAssets(const uuids::uuid& modelUUID,
                                                                  const uuids::uuid& textureUUID,
                                                                  const uuids::uuid& specularMapUUID)
{
  return { getModel(modelUUID), getTexture(textureUUID), getTexture(specularMapUUID) };
}

std::string GpuAssetCache::describeAsset(const uuids::uuid& uuid, const AssetType type) const
{
  const auto id = uuids::to_string(uuid);

  if (const auto record = m_assetRegistry->getByUUIDOfType(uuid, type))
  {
    return record->path + " (" + id + ")";
  }

  return id + " (not in the asset registry)";
}

void GpuAssetCache::reportMissingAssets(const GpuRenderAssets& resolved, const uuids::uuid& modelUUID,
                                        const uuids::uuid& textureUUID, const uuids::uuid& specularMapUUID)
{
  const auto report = [this](const bool missing, const uuids::uuid& uuid, const AssetType type) {
    if (missing)
    {
      m_missingAssets.report(uuids::to_string(uuid), describeAsset(uuid, type));
    }
  };

  report(!resolved.model, modelUUID, AssetType::Model);
  report(!resolved.texture, textureUUID, AssetType::Texture);
  report(!resolved.specularMap, specularMapUUID, AssetType::Texture);
}

GpuAssetCache::GpuRenderAssets GpuAssetCache::withMissingPlaceholders(const GpuRenderAssets& resolved)
{
  const auto placeholderModel = m_gizmoModels.get("assets/models/missing_model.glb", [this] {
    return m_renderer->getAssetManager()->loadModel("assets/models/missing_model.glb");
  });
  const auto placeholderTexture = m_gizmoTextures.get("assets/textures/missing_texture.png", [this] {
    return m_renderer->getAssetManager()->loadTexture("assets/textures/missing_texture.png");
  });

  return withPlaceholders(resolved, placeholderModel, placeholderTexture);
}

bool GpuAssetCache::hasUUIDs(const CachedRenderObject& cached, const uuids::uuid& modelUUID,
                             const uuids::uuid& textureUUID, const uuids::uuid& specularMapUUID)
{
  return cached.modelUUID == modelUUID && cached.textureUUID == textureUUID &&
         cached.specularMapUUID == specularMapUUID;
}

std::shared_ptr<vke::RenderObject> GpuAssetCache::storeRenderObject(const uuids::uuid& ownerUUID,
                                                                    const std::array<uuids::uuid, 3>& uuids,
                                                                    const GpuRenderAssets& drawn,
                                                                    const GpuRenderAssets& resolved)
{
  const auto assetManager = m_renderer->getAssetManager();
  auto renderObject = assetManager->loadRenderObject(drawn.texture, drawn.specularMap, drawn.model);

  if (const auto it = m_renderObjects.find(ownerUUID); it != m_renderObjects.end())
  {
    assetManager->release(std::move(it->second.renderObject));
  }

  m_renderObjects[ownerUUID] = { renderObject, uuids[0], uuids[1], uuids[2], !resolved.complete(), resolved };

  return renderObject;
}

std::shared_ptr<vke::RenderObject> GpuAssetCache::getRenderObject(const uuids::uuid& ownerUUID,
                                                                 const uuids::uuid& modelUUID,
                                                                 const uuids::uuid& textureUUID,
                                                                 const uuids::uuid& specularMapUUID)
{
  const auto it = m_renderObjects.find(ownerUUID);
  const bool sameUUIDs = it != m_renderObjects.end() &&
                         hasUUIDs(it->second, modelUUID, textureUUID, specularMapUUID);

  if (canReuseWithoutResolving(sameUUIDs, sameUUIDs && it->second.usedPlaceholder))
  {
    return it->second.renderObject;
  }

  const auto assets = resolveRenderAssets(modelUUID, textureUUID, specularMapUUID);
  if (sameUUIDs && canReuseAfterResolving(sameUUIDs, it->second.resolved, assets))
  {
    return it->second.renderObject;
  }

  reportMissingAssets(assets, modelUUID, textureUUID, specularMapUUID);

  const auto drawn = withMissingPlaceholders(assets);
  if (!drawn.complete())
  {
    return nullptr;
  }

  return storeRenderObject(ownerUUID, { modelUUID, textureUUID, specularMapUUID }, drawn, assets);
}

std::shared_ptr<vke::RenderObject> GpuAssetCache::getColliderGizmo(const uuids::uuid& ownerUUID,
                                                                   const std::string& modelPath,
                                                                   const std::size_t part)
{
  if (const auto it = m_colliderGizmos.find(ownerUUID);
      it != m_colliderGizmos.end() && part < it->second.size() && it->second[part].path == modelPath)
  {
    return it->second[part].renderObject;
  }

  const auto model = m_gizmoModels.get(modelPath, [this, &modelPath] {
    return m_renderer->getAssetManager()->loadModel(modelPath.c_str());
  });
  const auto white = m_gizmoTextures.get("assets/textures/white.png", [this] {
    return m_renderer->getAssetManager()->loadTexture("assets/textures/white.png");
  });

  if (!model || !white)
  {
    return nullptr;
  }

  auto renderObject = m_renderer->getAssetManager()->loadRenderObject(white, white, model);

  auto& parts = m_colliderGizmos[ownerUUID];
  if (parts.size() <= part)
  {
    parts.resize(part + 1);
  }
  parts[part] = { renderObject, modelPath };

  return renderObject;
}

void GpuAssetCache::trimColliderGizmo(const uuids::uuid& ownerUUID, const std::size_t partCount)
{
  const auto it = m_colliderGizmos.find(ownerUUID);
  if (it == m_colliderGizmos.end() || it->second.size() <= partCount)
  {
    return;
  }

  if (partCount == 0)
  {
    m_colliderGizmos.erase(it);
    return;
  }

  it->second.resize(partCount);
}

void GpuAssetCache::releaseHullGizmo(CachedHullGizmo& cached)
{
  const auto assetManager = m_renderer->getAssetManager();

  if (cached.renderObject)
  {
    assetManager->release(std::move(cached.renderObject));
  }

  if (cached.model)
  {
    assetManager->release(std::move(cached.model));
  }
}

std::shared_ptr<vke::RenderObject> GpuAssetCache::getHullGizmo(const uuids::uuid& ownerUUID, const ConvexHull& hull)
{
  const auto it = m_hullGizmos.find(ownerUUID);

  if (it != m_hullGizmos.end())
  {
    if (it->second.hull.vertices == hull.vertices && it->second.hull.indices == hull.indices)
    {
      return it->second.renderObject;
    }

    releaseHullGizmo(it->second);
    m_hullGizmos.erase(it);
  }

  CachedHullGizmo cached{ nullptr, nullptr, hull };

  const auto white = m_gizmoTextures.get("assets/textures/white.png", [this] {
    return m_renderer->getAssetManager()->loadTexture("assets/textures/white.png");
  });

  if (white)
  {
    const HullGizmoMesh mesh = buildHullGizmoMesh(hull);
    std::vector<vke::Vertex> vertices(mesh.positions.size());

    for (size_t i = 0; i < vertices.size(); ++i)
    {
      vertices[i].pos = mesh.positions[i];
      vertices[i].normal = mesh.normals[i];
      vertices[i].texCoord = glm::vec2(0.0f);
    }

    // An all-degenerate hull has nothing to draw, which is not an error.
    if (!mesh.indices.empty())
    {
      try
      {
        cached.model = m_renderer->getAssetManager()->createModel(vertices, mesh.indices);
        cached.renderObject = m_renderer->getAssetManager()->loadRenderObject(white, white, cached.model);
      }
      catch (const std::exception& e)
      {
        releaseHullGizmo(cached);

        Log::warn(LogCategory::assets, std::string("Could not build the convex hull gizmo: ") + e.what());
      }
    }
  }

  // A failed or empty build is kept so it is not retried (or logged again) every frame until the hull changes.
  const auto renderObject = cached.renderObject;
  m_hullGizmos.insert_or_assign(ownerUUID, std::move(cached));

  return renderObject;
}

void GpuAssetCache::dropHullGizmo(const uuids::uuid& ownerUUID)
{
  if (const auto it = m_hullGizmos.find(ownerUUID); it != m_hullGizmos.end())
  {
    releaseHullGizmo(it->second);
    m_hullGizmos.erase(it);
  }
}

void GpuAssetCache::loadLightGizmoAssets()
{
  if (m_lightGizmoAssetsTried)
  {
    return;
  }

  m_lightGizmoAssetsTried = true;

  try
  {
    m_lightGizmoModel = m_renderer->getAssetManager()->loadModel("assets/models/billboard_quad.glb");
    m_lightGizmoIcon = m_renderer->getAssetManager()->loadTexture("assets/textures/light_gizmo.png", false);
  }
  catch (const std::exception& e)
  {
    m_lightGizmoModel.reset();
    m_lightGizmoIcon.reset();

    Log::error(LogCategory::engine, std::string("Could not load the light sprite assets ") +
      "(assets/models/billboard_quad.glb, assets/textures/light_gizmo.png); lights will have no sprite: " + e.what());
  }
}

std::shared_ptr<vke::RenderObject> GpuAssetCache::getLightGizmo(const uuids::uuid& ownerUUID)
{
  if (const auto it = m_lightGizmos.find(ownerUUID); it != m_lightGizmos.end())
  {
    return it->second;
  }

  loadLightGizmoAssets();

  if (!m_lightGizmoModel || !m_lightGizmoIcon)
  {
    return nullptr;
  }

  auto renderObject = m_renderer->getAssetManager()->loadRenderObject(m_lightGizmoIcon, m_lightGizmoIcon,
                                                                      m_lightGizmoModel);

  m_lightGizmos[ownerUUID] = renderObject;

  return renderObject;
}

void GpuAssetCache::applyPendingReset()
{
  if (!m_resetPending)
  {
    return;
  }

  m_resetPending = false;

  const auto assetManager = m_renderer->getAssetManager();

  for (auto& entry : m_renderObjects)
  {
    assetManager->release(std::move(entry.second.renderObject));
  }
  m_renderObjects.clear();
  m_missingAssets.clear();

  m_models.drain([&assetManager](std::shared_ptr<vke::Model> model) { assetManager->release(std::move(model)); });
  m_textures.drain([&assetManager](std::shared_ptr<vke::Texture2D> texture) {
    assetManager->release(std::move(texture));
  });
}

void GpuAssetCache::pruneStale(const std::unordered_set<uuids::uuid>& liveUUIDs)
{
  std::erase_if(m_renderObjects, [&liveUUIDs](const auto& entry) { return !liveUUIDs.contains(entry.first); });
  std::erase_if(m_colliderGizmos, [&liveUUIDs](const auto& entry) { return !liveUUIDs.contains(entry.first); });
  for (auto it = m_hullGizmos.begin(); it != m_hullGizmos.end();)
  {
    if (liveUUIDs.contains(it->first))
    {
      ++it;
      continue;
    }

    releaseHullGizmo(it->second);
    it = m_hullGizmos.erase(it);
  }

  std::erase_if(m_lightGizmos, [&liveUUIDs](const auto& entry) { return !liveUUIDs.contains(entry.first); });
}
