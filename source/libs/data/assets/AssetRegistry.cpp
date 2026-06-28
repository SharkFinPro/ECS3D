#include "AssetRegistry.h"
#include <nlohmann/json.hpp>
#include <Protocol.h>
#include <vector>

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

  ++m_version;
}

void AssetRegistry::clear()
{
  m_assets.clear();
  m_loadedPaths.clear();
  ++m_version;
}

size_t AssetRegistry::getVersion() const
{
  return m_version;
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

void AssetRegistry::pack(net::Message& message) const
{
  // Collect first so we can write a count up front (m_assets also holds Scene records, which — like
  // serialize() — are skipped here; they're carried by the SceneManager).
  std::vector<const AssetRecord*> records;
  for (const auto& [uuid, record] : m_assets)
  {
    if (record.type == AssetType::Model || record.type == AssetType::Texture
        || record.type == AssetType::Script)
    {
      records.push_back(&record);
    }
  }

  message.write(static_cast<uint32_t>(records.size()));
  for (const auto* record : records)
  {
    message.write(record->type);
    message.writeString(uuids::to_string(record->uuid));
    message.writeString(record->path);
    message.writeString(record->className); // empty for non-scripts
  }
}

void AssetRegistry::unpack(net::MessageReader& messageReader)
{
  const uint32_t count = messageReader.read<uint32_t>();
  for (uint32_t i = 0; i < count; ++i)
  {
    const auto type = messageReader.read<AssetType>();
    const auto uuid = uuids::uuid::from_string(messageReader.readString()).value();
    const auto path = messageReader.readString();
    const auto className = messageReader.readString();

    registerAsset({ .uuid = uuid, .type = type, .path = path, .className = className });
  }
}
