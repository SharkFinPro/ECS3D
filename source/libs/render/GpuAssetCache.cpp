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

  // Was ModelAsset::load(): renderer->getAssetManager()->loadModel(path).
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

  // Was TextureAsset::load(): renderer->getAssetManager()->loadTexture(path).
  auto texture = m_renderer->getAssetManager()->loadTexture(record->path.c_str());

  m_textures.emplace(uuid, texture);

  return texture;
}

std::shared_ptr<vke::RenderObject> GpuAssetCache::getRenderObject(const uuids::uuid& modelUUID,
                                                                 const uuids::uuid& textureUUID,
                                                                 const uuids::uuid& specularMapUUID)
{
  const auto key = uuids::to_string(modelUUID) + uuids::to_string(textureUUID) + uuids::to_string(specularMapUUID);

  if (const auto it = m_renderObjects.find(key); it != m_renderObjects.end())
  {
    return it->second;
  }

  const auto model = getModel(modelUUID);
  const auto texture = getTexture(textureUUID);
  const auto specularMap = getTexture(specularMapUUID);

  if (!model || !texture || !specularMap)
  {
    return nullptr;
  }

  // Was the ModelRenderer ctor: loadRenderObject(texture, specularMap, model).
  auto renderObject = m_renderer->getAssetManager()->loadRenderObject(texture, specularMap, model);

  m_renderObjects.emplace(key, renderObject);

  return renderObject;
}
