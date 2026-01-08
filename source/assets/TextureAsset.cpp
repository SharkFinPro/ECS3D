#include "TextureAsset.h"
#include "AssetManager.h"
#include "../ECS3D.h"
#include <VulkanEngine/VulkanEngine.h>
#include <VulkanEngine/components/assets/AssetManager.h>

TextureAsset::TextureAsset(const std::string& path)
  : Asset(path), m_path(path)
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
  m_texture =  assetManager->getECS()->getRenderer()->getAssetManager()->loadTexture(m_path.c_str());
}

void TextureAsset::displayGui()
{
  Asset::displayGui();

  ImGui::ImageButton("img", m_texture->getImGuiTexture(), {150, 150});
}
