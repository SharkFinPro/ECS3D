#include "AssetManager.h"
#include "Asset.h"
#include "ModelAsset.h"
#include "SceneAsset.h"
#include "ScriptAsset.h"
#include "TextureAsset.h"
#include "../objects/Object.h"
#include "../objects/ObjectManager.h"
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <VulkanEngine/components/window/Window.h>
#include <ranges>

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

  displayAssetsFilterGui();

  computeFilteredAssets();

  constexpr int cellSize = 150;
  const float scaledCellSize = cellSize * m_ecs->getRenderer()->getWindow()->getContentScale();
  const float width = ImGui::GetContentRegionAvail().x;

  ImGui::Columns(std::max(1, static_cast<int>(width / scaledCellSize)), 0, false);

  for (const auto& asset : m_filteredAssets)
  {
    ImGui::PushID(&asset);

    asset->displayGui(scaledCellSize * 0.75f);

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    {
      ImGui::SetDragDropPayload("asset", &asset, sizeof(asset));
      ImGui::Text("%s", asset->getName().c_str());
      ImGui::EndDragDropSource();
    }

    ImGui::PopID();

    ImGui::NextColumn();
  }

  ImGui::End();
}

std::shared_ptr<SceneAsset> AssetManager::createSceneAsset(std::string name)
{
  const uuids::uuid uuid = m_ecs->createUUID();

  const auto asset = std::make_shared<SceneAsset>(uuid, std::move(name));
  asset->setManager(this);
  asset->load();

  m_assets.emplace(uuid, asset);

  m_shouldComputeFilteredAssets = true;

  return asset;
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

  m_shouldComputeFilteredAssets = true;
}

nlohmann::json AssetManager::serialize()
{
  nlohmann::json data = {
    { "models", nlohmann::json::array() },
    { "textures", nlohmann::json::array() },
    { "scenes", nlohmann::json::array() },
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
    else if (const auto sceneAsset = std::dynamic_pointer_cast<SceneAsset>(asset))
    {
      data["scenes"].push_back(sceneAsset->serialize());
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
  loadModelsFromJSON(assetsData);

  loadTexturesFromJSON(assetsData);

  loadScenesFromJSON(assetsData);

  loadScriptsFromJSON(assetsData);

  m_shouldComputeFilteredAssets = true;
}

void AssetManager::displayAssetsFilterGui()
{
  if (ImGui::CollapsingHeader("Options"))
  {
    ImGui::Spacing();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Filter: ");
    for (const auto& [type, typeStr] : assetTypeToString)
    {
      bool selected = m_filteredAssetType == type;

      ImGui::SameLine();

      if (ImGui::Checkbox(typeStr.c_str(), &selected))
      {
        m_filteredAssetType = selected ? type : AssetType::Unknown;

        m_shouldComputeFilteredAssets = true;
      }
    }

    char searchBuf[64] = {};
    m_searchQuery.copy(searchBuf, sizeof(searchBuf) - 1);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Search: ");
    ImGui::SameLine();
    if (ImGui::InputText("##Search", searchBuf, sizeof(searchBuf)))
    {
      m_searchQuery = searchBuf;

      m_shouldComputeFilteredAssets = true;
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Sort: ");
    ImGui::SameLine();
    if (ImGui::BeginCombo("##SortCombo", sortTypeToString.at(m_sortType).c_str()))
    {
      if (ImGui::Selectable(sortTypeToString.at(SortType::nameAscending).c_str(), m_sortType == SortType::nameAscending))
      {
        m_sortType = SortType::nameAscending;

        m_shouldComputeFilteredAssets = true;
      }

      if (ImGui::Selectable(sortTypeToString.at(SortType::nameDescending).c_str(), m_sortType == SortType::nameDescending))
      {
        m_sortType = SortType::nameDescending;

        m_shouldComputeFilteredAssets = true;
      }

      ImGui::EndCombo();
    }
  }

  ImGui::Separator();
}

void AssetManager::computeFilteredAssets()
{
  if (!m_shouldComputeFilteredAssets)
  {
    return;
  }

  m_filteredAssets.clear();

  for (const auto& [_, asset] : m_assets)
  {
    if (m_filteredAssetType != AssetType::Unknown && asset->getAssetType() != m_filteredAssetType)
    {
      continue;
    }

    if (!m_searchQuery.empty())
    {
      const std::string& assetName = asset->getName();

      const bool match = std::search(assetName.begin(), assetName.end(), m_searchQuery.begin(),
                                     m_searchQuery.end(), [](char a, char b) {
        return std::tolower(a) == std::tolower(b);
      }) != assetName.end();

      if (!match)
      {
        continue;
      }
    }

    m_filteredAssets.emplace_back(asset);
  }

  std::ranges::sort(m_filteredAssets, [this](const auto& a, const auto& b) {
    return std::ranges::lexicographical_compare(a->getName(), b->getName(), [this](const char x, const char y) {
      if (m_sortType == SortType::nameAscending)
      {
        return std::tolower(x) < std::tolower(y);
      }

      if (m_sortType == SortType::nameDescending)
      {
        return std::tolower(x) > std::tolower(y);
      }

      return true;
    });
  });

  m_shouldComputeFilteredAssets = false;
}

void AssetManager::loadModelsFromJSON(const nlohmann::json& assetsData)
{
  if (!assetsData.contains("models"))
  {
    return;
  }

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
}

void AssetManager::loadTexturesFromJSON(const nlohmann::json& assetsData)
{
  if (!assetsData.contains("textures"))
  {
    return;
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
}

void AssetManager::loadScenesFromJSON(const nlohmann::json& assetsData)
{
  if (!assetsData.contains("scenes"))
  {
    return;
  }

  for (const auto& sceneData : assetsData.at("scenes"))
  {
    uuids::uuid uuid = uuids::uuid::from_string(std::string(sceneData.at("uuid"))).value();
    const auto& name = sceneData.at("name");

    const auto asset = std::make_shared<SceneAsset>(uuid, name);
    asset->setManager(this);
    asset->load();

    auto objectManager = asset->getObjectManager();

    for (const auto& objectData : sceneData.at("objects"))
    {
      auto object = std::make_shared<Object>(objectData, objectManager.get());
      objectManager->addObject(object);

      if (objectData.contains("children"))
      {
        object->loadChildren(objectData.at("children"));
      }
    }

    m_assets.emplace(uuid, asset);
  }
}

void AssetManager::loadScriptsFromJSON(const nlohmann::json& assetsData)
{
  if (!assetsData.contains("scripts"))
  {
    return;
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
