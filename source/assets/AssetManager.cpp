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

  for (const auto& asset : assets)
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

  assets.push_back(texture);
}
