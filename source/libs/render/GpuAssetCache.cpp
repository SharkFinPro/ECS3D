#include "GpuAssetCache.h"
#include <assets/AssetRegistry.h>
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/assets/AssetManager.h>
#include <VulkanEngine/components/assets/objects/Model.h>
#include <VulkanEngine/components/assets/objects/RenderObject.h>
#include <VulkanEngine/components/assets/textures/Texture2D.h>

GpuAssetCache::GpuAssetCache(std::shared_ptr<vke::VulkanEngine> renderer, const AssetRegistry* assetRegistry)
  : m_renderer(std::move(renderer)), m_assetRegistry(assetRegistry)
{}

std::shared_ptr<vke::VulkanEngine> GpuAssetCache::getRenderer() const
{
  return m_renderer;
}

std::shared_ptr<vke::Model> GpuAssetCache::getModel(const uuids::uuid& uuid)
{
  if (const auto it = m_models.find(uuid); it != m_models.end())
  {
    return it->second;
  }

  const auto record = m_assetRegistry->getByUUID(uuid);
  if (!record)
  {
    return nullptr;
  }

  auto model = m_renderer->getAssetManager()->loadModel(record->path.c_str());

  m_models.emplace(uuid, model);

  return model;
}

std::shared_ptr<vke::Texture2D> GpuAssetCache::getTexture(const uuids::uuid& uuid)
{
  if (const auto it = m_textures.find(uuid); it != m_textures.end())
  {
    return it->second;
  }

  const auto record = m_assetRegistry->getByUUID(uuid);
  if (!record)
  {
    return nullptr;
  }

  auto texture = m_renderer->getAssetManager()->loadTexture(record->path.c_str());

  m_textures.emplace(uuid, texture);

  return texture;
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

std::shared_ptr<vke::RenderObject> GpuAssetCache::getColliderGizmo(const uuids::uuid& ownerUUID, const std::string& modelPath)
{
  if (const auto it = m_colliderGizmos.find(ownerUUID); it != m_colliderGizmos.end() && it->second.path == modelPath)
  {
    return it->second.renderObject;
  }

  const auto model = m_renderer->getAssetManager()->loadModel(modelPath.c_str());
  const auto white = m_renderer->getAssetManager()->loadTexture("assets/textures/white.png");

  if (!model || !white)
  {
    return nullptr;
  }

  auto renderObject = m_renderer->getAssetManager()->loadRenderObject(white, white, model);

  m_colliderGizmos[ownerUUID] = { renderObject, modelPath };

  return renderObject;
}
