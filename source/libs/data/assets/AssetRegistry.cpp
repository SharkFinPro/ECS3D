#include "AssetRegistry.h"
#include <nlohmann/json.hpp>
#include <Protocol.h>
#include <vector>

void AssetRegistry::registerAsset(const AssetRecord& record)
{
  if (const auto pathIt = m_loadedPaths.find(record.path);
      !record.path.empty() && pathIt != m_loadedPaths.end())
  {
    // A prefab is the one asset whose payload can change after registration: re-saving an object under an
    // existing prefab name updates the body in place and KEEPS the original uuid, so a script or scene
    // holding that uuid keeps working. Every other type is first-wins (re-importing a file is a no-op).
    if (const auto existing = m_assets.find(pathIt->second);
        record.type == AssetType::Prefab && existing != m_assets.end()
        && existing->second.type == AssetType::Prefab)
    {
      existing->second.body = record.body;
      ++m_version;
    }

    return;
  }

  m_assets.emplace(record.uuid, record);

  if (!record.path.empty())
  {
    m_loadedPaths.emplace(record.path, record.uuid);
  }

  ++m_version;
}

void AssetRegistry::renameAsset(const uuids::uuid& uuid, const std::string& displayName)
{
  const auto it = m_assets.find(uuid);
  if (it == m_assets.end())
  {
    return;
  }

  // Display-only: `path` (the key, and the name-key for prefabs/scenes) stays put, so nothing that
  // resolves the asset by path or uuid is disturbed — only the shown name changes.
  it->second.displayName = displayName;
  ++m_version;
}

void AssetRegistry::removeAsset(const uuids::uuid& uuid)
{
  const auto it = m_assets.find(uuid);
  if (it == m_assets.end())
  {
    return;
  }

  // Free the path key too so its name (a prefab/scene display name, or a file path) can be registered
  // again later. Guard on the mapping actually pointing back at this uuid — a first-wins collision earlier
  // may have left the key owned by a different record.
  if (const auto pathIt = m_loadedPaths.find(it->second.path);
      pathIt != m_loadedPaths.end() && pathIt->second == uuid)
  {
    m_loadedPaths.erase(pathIt);
  }

  m_assets.erase(it);
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

nlohmann::json AssetRegistry::getPrefabBody(const uuids::uuid& uuid) const
{
  const auto* record = getByUUID(uuid);
  if (!record || record->type != AssetType::Prefab || record->body.empty())
  {
    return nullptr;
  }

  // The body is stored dumped (like Script::m_fields on the wire); parse leniently so a corrupt blob
  // yields a null json the caller skips, rather than throwing inside the server's tick loop.
  auto body = nlohmann::json::parse(record->body, nullptr, false);

  return body.is_discarded() || !body.is_object() ? nlohmann::json(nullptr) : body;
}

nlohmann::json AssetRegistry::serialize() const
{
  // Scenes are NOT serialized here — they carry their object tree and belong to the SceneManager.
  // ProjectSerializer merges this with the scene data.
  nlohmann::json data = {
    { "models", nlohmann::json::array() },
    { "textures", nlohmann::json::array() },
    { "scripts", nlohmann::json::array() },
    { "prefabs", nlohmann::json::array() }
  };

  // A rename sets a display-name override on the record; write it (append only, omitted when empty) so a
  // renamed asset survives a save/load. The file on disk and `path` never change — only this override.
  const auto withDisplayName = [](nlohmann::json entry, const AssetRecord& record) {
    if (!record.displayName.empty())
    {
      entry["displayName"] = record.displayName;
    }
    return entry;
  };

  for (const auto& [uuid, record] : m_assets)
  {
    const auto uuidString = uuids::to_string(uuid);

    switch (record.type)
    {
      case AssetType::Model:
        data["models"].push_back(withDisplayName({ { "name", record.path }, { "filePath", record.path }, { "uuid", uuidString } }, record));
        break;
      case AssetType::Texture:
        data["textures"].push_back(withDisplayName({ { "name", record.path }, { "filePath", record.path }, { "uuid", uuidString } }, record));
        break;
      case AssetType::Script:
        data["scripts"].push_back(withDisplayName({ { "className", record.className }, { "filePath", record.path }, { "uuid", uuidString } }, record));
        break;
      case AssetType::Prefab:
      {
        // The body travels with the record. Embed it as real JSON (not the dumped string) so the project
        // file stays readable/editable; loadFromJSON dumps it back. A discarded parse would throw on
        // dump(), so a corrupt body is written as null and dropped on the next load.
        auto body = nlohmann::json::parse(record.body, nullptr, false);

        data["prefabs"].push_back(withDisplayName({
          { "name", record.path },
          { "uuid", uuidString },
          { "body", body.is_discarded() ? nlohmann::json(nullptr) : std::move(body) }
        }, record));
        break;
      }
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
        .path = assetData.at("filePath"),
        .displayName = assetData.value("displayName", std::string{})
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
        .path = assetData.at("filePath"),
        .displayName = assetData.value("displayName", std::string{})
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
        .className = assetData.at("className"),
        .displayName = assetData.value("displayName", std::string{})
      });
    }
  }

  if (assetsData.contains("prefabs"))
  {
    for (const auto& assetData : assetsData.at("prefabs"))
    {
      // A prefab without a usable body is nothing — drop it rather than register a record that can never
      // instantiate (serialize() writes null for a body it couldn't parse).
      if (!assetData.contains("body") || !assetData.at("body").is_object())
      {
        continue;
      }

      registerAsset({
        .uuid = uuids::uuid::from_string(std::string(assetData.at("uuid"))).value(),
        .type = AssetType::Prefab,
        .path = assetData.at("name"),                 // prefabs key off their display name
        .body = assetData.at("body").dump(),
        .displayName = assetData.value("displayName", std::string{})
      });
    }
  }
}

void AssetRegistry::pack(net::Message& message) const
{
  // Collect first so we can write a count up front (m_assets also holds Scene records, which — like
  // serialize() — are skipped here; they're carried by the SceneManager). A Prefab carries its whole body
  // inline; every other record writes an empty body string, keeping the entry layout uniform.
  std::vector<const AssetRecord*> records;
  for (const auto& [uuid, record] : m_assets)
  {
    if (record.type == AssetType::Model || record.type == AssetType::Texture
        || record.type == AssetType::Script || record.type == AssetType::Prefab)
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
    message.writeString(record->className);   // empty for non-scripts
    message.writeString(record->body);        // empty for non-prefabs
    message.writeString(record->displayName); // empty unless renamed (append-only wire field)
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
    const auto body = messageReader.readString();
    const auto displayName = messageReader.readString();

    registerAsset({ .uuid = uuid, .type = type, .path = path, .className = className, .body = body,
                    .displayName = displayName });
  }
}
