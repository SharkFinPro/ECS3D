#include "AssetRegistry.h"
#include <nlohmann/json.hpp>

void AssetRegistry::registerAsset(const AssetRecord& record)
{
  if (!record.path.empty() && m_loadedPaths.contains(record.path))
  {
    return;
  }

  m_assets.emplace(record.uuid, record);

  if (!record.path.empty())
  {
    m_loadedPaths.emplace(record.path, record.uuid);
  }
}

void AssetRegistry::clear()
{
  m_assets.clear();
  m_loadedPaths.clear();
}

const AssetRecord* AssetRegistry::getByUUID(const uuids::uuid& uuid) const
{
  const auto it = m_assets.find(uuid);

  return it != m_assets.end() ? &it->second : nullptr;
}

const AssetRecord* AssetRegistry::getByPath(const std::string& path) const
{
  const auto pathIt = m_loadedPaths.find(path);
  if (pathIt == m_loadedPaths.end())
  {
    return nullptr;
  }

  return getByUUID(pathIt->second);
}

const std::unordered_map<uuids::uuid, AssetRecord>& AssetRegistry::getAssets() const
{
  return m_assets;
}

nlohmann::json AssetRegistry::serialize() const
{
  // Scenes are NOT serialized here — they carry their object tree and belong to the SceneManager
  // migration. The future ProjectSerializer merges this with the scene data.
  nlohmann::json data = {
    { "models", nlohmann::json::array() },
    { "textures", nlohmann::json::array() },
    { "scripts", nlohmann::json::array() }
  };

  for (const auto& [uuid, record] : m_assets)
  {
    const auto uuidString = uuids::to_string(uuid);

    switch (record.type)
    {
      case AssetType::Model:
        data["models"].push_back({ { "name", record.path }, { "filePath", record.path }, { "uuid", uuidString } });
        break;
      case AssetType::Texture:
        data["textures"].push_back({ { "name", record.path }, { "filePath", record.path }, { "uuid", uuidString } });
        break;
      case AssetType::Script:
        data["scripts"].push_back({ { "className", record.className }, { "filePath", record.path }, { "uuid", uuidString } });
        break;
      default:
        break;
    }
  }

  return data;
}

void AssetRegistry::loadFromJSON(const nlohmann::json& assetsData)
{
  if (assetsData.contains("models"))
  {
    for (const auto& assetData : assetsData.at("models"))
    {
      registerAsset({
        .uuid = uuids::uuid::from_string(std::string(assetData.at("uuid"))).value(),
        .type = AssetType::Model,
        .path = assetData.at("filePath")
      });
    }
  }

  if (assetsData.contains("textures"))
  {
    for (const auto& assetData : assetsData.at("textures"))
    {
      registerAsset({
        .uuid = uuids::uuid::from_string(std::string(assetData.at("uuid"))).value(),
        .type = AssetType::Texture,
        .path = assetData.at("filePath")
      });
    }
  }

  if (assetsData.contains("scripts"))
  {
    for (const auto& assetData : assetsData.at("scripts"))
    {
      registerAsset({
        .uuid = uuids::uuid::from_string(std::string(assetData.at("uuid"))).value(),
        .type = AssetType::Script,
        .path = assetData.at("filePath"),
        .className = assetData.at("className")
      });
    }
  }
}
