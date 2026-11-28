#include "GpuAssetCache.h"
#include "HullGizmoMesh.h"
#include "TerrainSurfacePlan.h"
#include "TerrainVolumeSource.h"
#include <assets/AssetRegistry.h>
#include <ProjectPaths.h>
#include <Log.h>
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/assets/AssetManager.h>
#include <VulkanEngine/components/pipelines/implementations/vertexInputs/Vertex.h>
#include <VulkanEngine/components/assets/objects/Model.h>
#include <VulkanEngine/components/assets/objects/RenderObject.h>
#include <VulkanEngine/components/assets/textures/Texture2D.h>
#include <VulkanEngine/components/assets/volume/VolumeSource.h>
#include <VulkanEngine/components/assets/volume/VolumeSurface.h>
#include <VulkanEngine/components/renderingManager/RenderingManager.h>
#include <VulkanEngine/components/renderingManager/renderer3D/MousePicker.h>
#include <VulkanEngine/components/renderingManager/renderer3D/Renderer3D.h>
#include <array>
#include <cstddef>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

GpuAssetCache::GpuAssetCache(std::shared_ptr<vke::VulkanEngine> renderer, const AssetRegistry* assetRegistry)
  : m_renderer(std::move(renderer)), m_assetRegistry(assetRegistry)
{
  // Both apps build their cache once from the renderer, and RenderSystem::requestPick is the only pick path.
  m_renderer->getRenderingManager()->getRenderer3D()->getMousePicker()->setPickMode(vke::PickMode::onRequest);
}

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

bool GpuAssetCache::claimTransformPush(const uuids::uuid& ownerUUID, const uint64_t worldUpdateID)
{
  const auto it = m_renderObjects.find(ownerUUID);

  return it == m_renderObjects.end() || it->second.transformPush.needsPush(worldUpdateID);
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

static_assert(maxTerrainPaletteMaterials == vke::s_maxVolumeMaterials);

namespace {
  vke::VolumeRegion toRegion(const terrain::SampleBox& box)
  {
    return { box.min, box.max };
  }
}

void GpuAssetCache::releaseTerrainSurfaces(CachedTerrainSurface& cached)
{
  const auto assetManager = m_renderer->getAssetManager();

  if (cached.current)
  {
    assetManager->release(std::move(cached.current));
  }

  if (cached.replacement)
  {
    assetManager->release(std::move(cached.replacement));
  }
}

void GpuAssetCache::createTerrainSurface(const uuids::uuid& terrainUUID, CachedTerrainSurface& cached,
                                         const terrain::SampleBox& bounds, const bool asReplacement)
{
  try
  {
    auto surface = m_renderer->getAssetManager()->createVolumeSurface(
      { .cellSize = cached.voxelSize, .chunkCells = terrain::surfaceChunkCells },
      TerrainVolumeSource::forSurface(cached.feed, bounds, cached.openSides));

    if (asReplacement)
    {
      cached.replacement = std::move(surface);
      cached.replacementBounds = bounds;
      cached.replacementPalette.reset();
    }
    else
    {
      cached.current = std::move(surface);
      cached.currentBounds = bounds;
      cached.currentPalette.reset();
    }

    cached.failedFor.reset();
  }
  catch (const std::exception& e)
  {
    cached.failedFor = bounds;

    Log::warn(LogCategory::engine, "Could not create the surface for terrain " + uuids::to_string(terrainUUID) +
      ": " + e.what());
  }
}

void GpuAssetCache::applyTerrainPalette(const uuids::uuid& terrainUUID, CachedTerrainSurface& cached,
                                        const std::span<const terrain::TerrainMaterial> palette)
{
  if (!cached.current && !cached.replacement)
  {
    return;
  }

  if (palette.size() > maxTerrainPaletteMaterials && !cached.loggedPaletteTruncated)
  {
    cached.loggedPaletteTruncated = true;

    Log::warn(LogCategory::assets, "Terrain " + uuids::to_string(terrainUUID) + " has " +
      std::to_string(palette.size()) + " palette materials; only the first " +
      std::to_string(maxTerrainPaletteMaterials) + " are drawn");
  }

  if (cached.paletteInput.inputChanged(palette))
  {
    cached.loggedPaletteFailure = false;
  }

  if (cached.paletteInput.needsResolve(palette))
  {
    bool usedPlaceholder = false;

    cached.resolvedPalette = resolveTerrainPalette<vke::Texture2D>(
      palette, [this](const uuids::uuid& uuid) { return getTexture(uuid); },
      [this] {
        return m_gizmoTextures.get("assets/textures/missing_texture.png", [this] {
          return m_renderer->getAssetManager()->loadTexture("assets/textures/missing_texture.png");
        });
      },
      [this, &usedPlaceholder](const uuids::uuid& uuid) {
        usedPlaceholder = true;
        m_missingAssets.report(uuids::to_string(uuid), describeAsset(uuid, AssetType::Texture));
      });

    cached.paletteInput.record(palette, usedPlaceholder);
  }

  const auto& resolved = cached.resolvedPalette;

  const auto applyTo = [&](const std::shared_ptr<vke::VolumeSurface>& surface,
                           TerrainPaletteApplication<vke::Texture2D>& applied) {
    if (!surface || !applied.needsApply(resolved))
    {
      return;
    }

    // setPalette throws and changes nothing, so recording first keeps a rejected palette from being retried (and
    // its texture arrays rebuilt) every frame.
    applied.markApplied(resolved);

    std::vector<vke::VolumeMaterial> materials;
    materials.reserve(resolved.size());
    for (const auto& entry : resolved)
    {
      materials.push_back({ .albedo = entry.albedo, .color = entry.color, .specular = entry.specular,
                            .textureScale = entry.textureScale });
    }

    try
    {
      surface->setPalette(materials);
    }
    catch (const std::exception& e)
    {
      if (!cached.loggedPaletteFailure)
      {
        cached.loggedPaletteFailure = true;

        Log::warn(LogCategory::assets, "Could not apply the palette of terrain " + uuids::to_string(terrainUUID) +
          ": " + e.what());
      }
    }
  };

  applyTo(cached.current, cached.currentPalette);
  applyTo(cached.replacement, cached.replacementPalette);
}

std::shared_ptr<vke::VolumeSurface> GpuAssetCache::getTerrainSurface(
  const uuids::uuid& terrainUUID, const std::shared_ptr<const terrain::TerrainVolume>& volume, const uint8_t openSides,
  const std::span<const terrain::TerrainMaterial> palette)
{
  if (!volume)
  {
    return nullptr;
  }

  auto it = m_terrainSurfaces.find(terrainUUID);

  if (it != m_terrainSurfaces.end() &&
      (it->second.voxelSize != volume->voxelSize() || it->second.openSides != openSides))
  {
    releaseTerrainSurfaces(it->second);
    m_terrainSurfaces.erase(it);
    it = m_terrainSurfaces.end();
  }

  if (it == m_terrainSurfaces.end())
  {
    CachedTerrainSurface fresh;
    fresh.snapshot = std::make_shared<const terrain::TerrainVolume::ChunkMap>(volume->snapshotChunks());
    fresh.feed = std::make_shared<TerrainSurfaceFeed>(fresh.snapshot);
    fresh.voxelSize = volume->voxelSize();
    fresh.openSides = openSides;
    fresh.revision = volume->revision();
    fresh.desired = terrain::desiredSurfaceBounds(volume->occupiedBounds(), openSides);

    it = m_terrainSurfaces.emplace(terrainUUID, std::move(fresh)).first;
  }

  auto& cached = it->second;

  if (cached.revision != volume->revision())
  {
    auto next = std::make_shared<const terrain::TerrainVolume::ChunkMap>(volume->snapshotChunks());
    const auto changed = terrain::changedChunks(*cached.snapshot, *next);
    const terrain::SampleBox region = terrain::chunkSampleBounds(changed);

    cached.feed->publish(next, [&] {
      if (region.empty())
      {
        return;
      }

      if (cached.current)
      {
        cached.current->invalidate(toRegion(region));
      }

      if (cached.replacement)
      {
        cached.replacement->invalidate(toRegion(region));
      }
    });

    cached.snapshot = std::move(next);
    cached.revision = volume->revision();

    const terrain::SampleBox desired = terrain::desiredSurfaceBounds(volume->occupiedBounds(), openSides);

    if (!terrain::sameBox(desired, cached.desired))
    {
      cached.desired = desired;
      cached.failedFor.reset();
      cached.loggedTooLarge = false;
    }
  }

  if (cached.failedFor && terrain::sameBox(*cached.failedFor, cached.desired))
  {
    applyTerrainPalette(terrainUUID, cached, palette);

    return cached.current;
  }

  terrain::SurfaceSlots slots;

  if (cached.current)
  {
    slots.current = cached.currentBounds;
  }

  if (cached.replacement)
  {
    slots.replacement = cached.replacementBounds;
    slots.replacementPending = cached.replacement->pendingFraction();
  }

  if (cached.current)
  {
    slots.currentPending = cached.current->pendingFraction();
  }

  const terrain::SurfacePlan plan = terrain::planTerrainSurface(cached.desired, slots, openSides != 0);

  switch (plan.step)
  {
    case terrain::SurfaceStep::createCurrent:
      createTerrainSurface(terrainUUID, cached, plan.bounds, false);
      break;
    case terrain::SurfaceStep::createReplacement:
      createTerrainSurface(terrainUUID, cached, plan.bounds, true);
      break;
    case terrain::SurfaceStep::promote:
      if (cached.current)
      {
        m_renderer->getAssetManager()->release(std::move(cached.current));
      }

      cached.current = std::move(cached.replacement);
      cached.currentBounds = cached.replacementBounds;
      cached.currentPalette = std::move(cached.replacementPalette);
      cached.replacementPalette.reset();
      cached.replacement.reset();
      break;
    case terrain::SurfaceStep::tooLarge:
      if (!cached.loggedTooLarge)
      {
        cached.loggedTooLarge = true;

        Log::warn(LogCategory::engine, "Terrain " + uuids::to_string(terrainUUID) + " needs " +
          std::to_string(terrain::surfaceChunkCount(plan.bounds)) + " surface chunks (more than " +
          std::to_string(terrain::maxSurfaceChunks) + "), so its surface is not drawn or grown");
      }
      break;
    case terrain::SurfaceStep::keep:
      break;
  }

  applyTerrainPalette(terrainUUID, cached, palette);

  return cached.current;
}

terrain::TerrainSurfaceStatus GpuAssetCache::terrainSurfaceStatus(const uuids::uuid& terrainUUID) const
{
  terrain::TerrainSurfaceStatus status;

  const auto it = m_terrainSurfaces.find(terrainUUID);
  if (it == m_terrainSurfaces.end())
  {
    return status;
  }

  const CachedTerrainSurface& cached = it->second;
  status.hasSurface = cached.current != nullptr;
  status.tooLarge = cached.loggedTooLarge;
  status.growing = cached.replacement != nullptr;

  if (cached.replacement)
  {
    status.pending = cached.replacement->pendingFraction();
  }
  else if (cached.current)
  {
    status.pending = cached.current->pendingFraction();
  }

  return status;
}

void GpuAssetCache::trimTerrainSurfaces(const std::unordered_set<uuids::uuid>& drawnTerrains)
{
  for (auto it = m_terrainSurfaces.begin(); it != m_terrainSurfaces.end();)
  {
    if (drawnTerrains.contains(it->first))
    {
      ++it;
      continue;
    }

    releaseTerrainSurfaces(it->second);
    it = m_terrainSurfaces.erase(it);
  }
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

  for (auto& entry : m_terrainSurfaces)
  {
    releaseTerrainSurfaces(entry.second);
  }
  m_terrainSurfaces.clear();

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
