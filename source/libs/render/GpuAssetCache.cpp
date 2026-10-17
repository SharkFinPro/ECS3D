#include "GpuAssetCache.h"
#include "HullGizmoMesh.h"
#include <assets/AssetRegistry.h>
#include <Log.h>
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/assets/AssetManager.h>
#include <VulkanEngine/components/pipelines/implementations/vertexInputs/Vertex.h>
#include <VulkanEngine/components/assets/objects/Model.h>
#include <VulkanEngine/components/assets/objects/RenderObject.h>
#include <VulkanEngine/components/assets/textures/Texture2D.h>
#include <cstddef>
#include <exception>
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

std::shared_ptr<vke::Model> GpuAssetCache::getModel(const uuids::uuid& uuid)
{
  const auto record = m_assetRegistry->getByUUID(uuid);
  if (!record)
  {
    return nullptr;
  }

  return m_models.get(uuids::to_string(uuid), record->path + " (" + uuids::to_string(uuid) + ")", [this, &record] {
    return m_renderer->getAssetManager()->loadModel(record->path.c_str());
  });
}

std::shared_ptr<vke::Texture2D> GpuAssetCache::getTexture(const uuids::uuid& uuid)
{
  const auto record = m_assetRegistry->getByUUID(uuid);
  if (!record)
  {
    return nullptr;
  }

  return m_textures.get(uuids::to_string(uuid), record->path + " (" + uuids::to_string(uuid) + ")", [this, &record] {
    return m_renderer->getAssetManager()->loadTexture(record->path.c_str());
  });
}

std::shared_ptr<vke::RenderObject> GpuAssetCache::getRenderObject(const uuids::uuid& ownerUUID,
                                                                 const uuids::uuid& modelUUID,
                                                                 const uuids::uuid& textureUUID,
                                                                 const uuids::uuid& specularMapUUID)
{
  if (const auto it = m_renderObjects.find(ownerUUID);
      it != m_renderObjects.end() &&
      it->second.modelUUID == modelUUID &&
      it->second.textureUUID == textureUUID &&
      it->second.specularMapUUID == specularMapUUID)
  {
    return it->second.renderObject;
  }

  const auto model = getModel(modelUUID);
  const auto texture = getTexture(textureUUID);
  const auto specularMap = getTexture(specularMapUUID);

  if (!model || !texture || !specularMap)
  {
    return nullptr;
  }

  auto renderObject = m_renderer->getAssetManager()->loadRenderObject(texture, specularMap, model);

  m_renderObjects[ownerUUID] = { renderObject, modelUUID, textureUUID, specularMapUUID };

  return renderObject;
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
