#include "AssetManager.h"
#include "Asset.h"
#include <imgui.h>

#include "TextureAsset.h"

AssetManager::AssetManager(ECS3D* ecs)
  : ecs(ecs)
{}

ECS3D* AssetManager::getECS() const
{
  return ecs;
}

void AssetManager::displayGui()
{
  ImGui::Begin("Assets");

  for (const auto& [name, asset] : assets)
  {
    asset->displayGui();
  }

  ImGui::End();
}

void AssetManager::loadTexture(const std::string& path)
{
  const auto texture = std::make_shared<TextureAsset>(path);
  texture->setManager(this);
  texture->load();

  assets.emplace(path, texture);
}

std::shared_ptr<Asset> AssetManager::getAsset(const std::string& path)
{
  return assets.at(path);
}

std::shared_ptr<TextureAsset> AssetManager::getTexture(const std::string &path)
{
  return std::dynamic_pointer_cast<TextureAsset>(getAsset(path));
}
