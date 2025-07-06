#include "TextureAsset.h"
#include "AssetManager.h"
#include "../ECS3D.h"
#include <VulkanEngine/VulkanEngine.h>

TextureAsset::TextureAsset(const std::string& path)
  : Asset(path), path(path)
{}

std::string TextureAsset::getPath()
{
  return path;
}

std::shared_ptr<Texture2D> TextureAsset::getTexture()
{
  return texture;
}

void TextureAsset::load()
{
  const auto renderer = assetManager->getECS()->getRenderer();

  texture = renderer->loadTexture(path.c_str());
}

void TextureAsset::displayGui()
{
  Asset::displayGui();

  ImGui::ImageButton("img", texture->getImGuiTexture(), {150, 150});
}
