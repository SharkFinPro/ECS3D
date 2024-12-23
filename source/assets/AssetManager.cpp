#include "AssetManager.h"
#include "Asset.h"
#include "TextureAsset.h"
#include "ModelAsset.h"
#include <imgui.h>

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

  constexpr int cellSize = 200;
  const int width = static_cast<int>(ImGui::GetContentRegionAvail().x);

  ImGui::Columns(width / cellSize, 0, false);

  for (const auto& [name, asset] : assets)
  {
    ImGui::PushID(&asset);

    asset->displayGui();

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    {
      ImGui::SetDragDropPayload("asset", &asset, sizeof(asset));
      ImGui::Text(name.c_str());
      ImGui::EndDragDropSource();
    }

    ImGui::PopID();

    ImGui::NextColumn();
  }

  ImGui::End();
}

void AssetManager::loadTexture(const std::string& path)
{
  if (assets.contains(path))
  {
    return;
  }

  const auto texture = std::make_shared<TextureAsset>(path);
  texture->setManager(this);
  texture->load();

  assets.emplace(path, texture);
}

void AssetManager::loadModel(const std::string& path)
{
  if (assets.contains(path))
  {
    return;
  }

  const auto model = std::make_shared<ModelAsset>(path);
  model->setManager(this);
  model->load();

  assets.emplace(path, model);
}

std::shared_ptr<Asset> AssetManager::getAsset(const std::string& path)
{
  return assets.at(path);
}

std::shared_ptr<TextureAsset> AssetManager::getTexture(const std::string& path)
{
  return std::dynamic_pointer_cast<TextureAsset>(getAsset(path));
}

std::shared_ptr<ModelAsset> AssetManager::getModel(const std::string& path)
{
  return std::dynamic_pointer_cast<ModelAsset>(getAsset(path));
}
