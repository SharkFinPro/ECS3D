#include "AssetManager.h"
#include "Asset.h"
#include "ModelAsset.h"
#include "ScriptAsset.h"
#include "TextureAsset.h"
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <VulkanEngine/components/window/Window.h>

AssetManager::AssetManager(ECS3D* ecs)
  : m_ecs(ecs)
{}

ECS3D* AssetManager::getECS() const
{
  return m_ecs;
}

void AssetManager::displayGui()
{
  ImGui::Begin("Assets");

  const int cellSize = 150 * m_ecs->getRenderer()->getWindow()->getContentScale();
  const int width = static_cast<int>(ImGui::GetContentRegionAvail().x);

  ImGui::Columns(std::max(1, width / cellSize), 0, false);

  for (const auto& [name, asset] : m_assets)
  {
    ImGui::PushID(&asset);

    asset->displayGui(cellSize * 0.75f);

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    {
      ImGui::SetDragDropPayload("asset", &asset, sizeof(asset));
      ImGui::Text(asset->getName().c_str());
      ImGui::EndDragDropSource();
    }

    ImGui::PopID();

    ImGui::NextColumn();
  }

  ImGui::End();
}

void AssetManager::loadScriptAsset(std::string path,
                                   std::string className)
{
  if (m_loadedPaths.contains(path))
  {
    return;
  }

  const uuids::uuid uuid = m_ecs->createUUID();

  const auto asset = std::make_shared<ScriptAsset>(uuid, path, std::move(className));
  asset->setManager(this);
  asset->load();

  m_assets.emplace(uuid, asset);

  m_loadedPaths.emplace(path, uuid);
}

nlohmann::json AssetManager::serialize()
{
  nlohmann::json data = {
    { "models", nlohmann::json::array() },
    { "textures", nlohmann::json::array() },
    { "scripts", nlohmann::json::array() }
  };

  for (const auto& [_, asset] : m_assets)
  {
    if (const auto textureAsset = std::dynamic_pointer_cast<TextureAsset>(asset))
    {
      data["textures"].push_back(textureAsset->serialize());
    }
    else if (const auto modelAsset = std::dynamic_pointer_cast<ModelAsset>(asset))
    {
      data["models"].push_back(modelAsset->serialize());
    }
    else if (const auto scriptAsset = std::dynamic_pointer_cast<ScriptAsset>(asset))
    {
      data["scripts"].push_back(scriptAsset->serialize());
    }
  }

  return data;
}

void AssetManager::loadFromJSON(const nlohmann::json& assetsData)
{
  for (const auto& assetData : assetsData.at("models"))
  {
    uuids::uuid uuid = uuids::uuid::from_string(std::string(assetData.at("uuid"))).value();
    const auto& path = assetData.at("filePath");

    const auto asset = std::make_shared<ModelAsset>(uuid, path);
    asset->setManager(this);
    asset->load();

    m_assets.emplace(uuid, asset);

    m_loadedPaths.emplace(path, uuid);
  }

  for (const auto& assetData : assetsData.at("textures"))
  {
    uuids::uuid uuid = uuids::uuid::from_string(std::string(assetData.at("uuid"))).value();
    const auto& path = assetData.at("filePath");

    const auto asset = std::make_shared<TextureAsset>(uuid, path);
    asset->setManager(this);
    asset->load();

    m_assets.emplace(uuid, asset);

    m_loadedPaths.emplace(path, uuid);
  }

  for (const auto& assetData : assetsData.at("scripts"))
  {
    uuids::uuid uuid = uuids::uuid::from_string(std::string(assetData.at("uuid"))).value();
    const auto& className = assetData.at("className");
    const auto& filePath = assetData.at("filePath");

    const auto asset = std::make_shared<ScriptAsset>(uuid, filePath, className);
    asset->setManager(this);
    asset->load();

    m_assets.emplace(uuid, asset);

    m_loadedPaths.emplace(filePath, uuid);
  }
}
