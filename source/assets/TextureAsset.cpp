#include "TextureAsset.h"
#include "AssetManager.h"
#include "../ECS3D.h"
#include <nlohmann/json.hpp>
#include <uuid.h>
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/assets/AssetManager.h>

TextureAsset::TextureAsset(const uuids::uuid uuid,
                           std::string path)
  : Asset(uuid, path.substr(path.find_last_of('/') + 1)), m_path(std::move(path))
{}

std::string TextureAsset::getPath()
{
  return m_path;
}

std::shared_ptr<vke::Texture2D> TextureAsset::getTexture()
{
  return m_texture;
}

void TextureAsset::load()
{
  m_texture = m_assetManager->getECS()->getRenderer()->getAssetManager()->loadTexture(m_path.c_str());
}

void TextureAsset::displayGui()
{
  Asset::displayGui();

  ImGui::ImageButton("img", m_texture->getImGuiTexture(), {150, 150});
}

nlohmann::json TextureAsset::serialize()
{
  const nlohmann::json data = {
    { "name", m_path },
    { "filePath", m_path },
    { "uuid", uuids::to_string(m_uuid) }
  };

  return data;
}
