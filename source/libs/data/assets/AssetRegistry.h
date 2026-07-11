#ifndef ASSETREGISTRY_H
#define ASSETREGISTRY_H

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <unordered_map>
#include <uuid.h>

namespace net {
  class Message;
  class MessageReader;
}

// Packed raw over the wire (AssetRegistry::pack), so only ever APPEND to this enum.
enum class AssetType {
  Unknown,
  Model,
  Texture,
  Scene,
  Script,
  Prefab
};

struct AssetRecord {
  uuids::uuid uuid;
  AssetType type = AssetType::Unknown;
  std::string path;        // scenes and prefabs store their display name here instead
  std::string className;   // scripts only
  std::string body;        // prefabs only: a serialized Object blob (Object::serialize().dump())
  std::string displayName; // optional rename override: when non-empty it replaces the derived display
                           // name (see assetDisplay::name). The file on disk and `path` (the registry
                           // key) never change — a rename is display-only.
};

// uuid<->path registry + serialize. Carries asset metadata only (no GPU resources, no GUI).
// ECS3DRender's GpuAssetCache resolves Model/Texture records to vke resources; the editor's
// AssetBrowserPanel renders the listing.
//
// A Prefab is the exception that carries its payload: the body travels inline (in the project file and
// the snapshot) rather than as a file on disk, because the authoritative server needs it to instantiate
// and may share no filesystem with the editor that made it. It is an opaque JSON blob threaded straight
// through serialize/pack, exactly like Script::m_fields.
class AssetRegistry {
public:
  // First-wins: re-registering an existing path keeps the original record. The one exception is a Prefab,
  // whose body is updated in place (keeping its uuid) — see registerAsset.
  void registerAsset(const AssetRecord& record);

  // Set a record's display-name override (a display-only rename; the file on disk and `path` — the
  // registry key — are untouched, so name-keyed prefabs/scenes keep their identity). No-op for an unknown
  // uuid. Bumps the version so cached views refresh.
  void renameAsset(const uuids::uuid& uuid, const std::string& displayName);

  // Drop a record by uuid, also clearing its `path` key (so a name-keyed prefab name frees up for reuse).
  // No-op for an unknown uuid. References to it dangle by design — lookups already null-tolerate a missing
  // uuid. Bumps the version.
  void removeAsset(const uuids::uuid& uuid);

  // Drop all records (used when (re)loading a project / applying a fresh snapshot).
  void clear();

  [[nodiscard]] const AssetRecord* getByUUID(const uuids::uuid& uuid) const;

  [[nodiscard]] const AssetRecord* getByPath(const std::string& path) const;

  [[nodiscard]] const std::unordered_map<uuids::uuid, AssetRecord>& getAssets() const;

  // The parsed body of a Prefab record, ready for ObjectManager::instantiate. Returns a null json when
  // the uuid isn't a registered prefab or its body is empty/malformed, so callers skip rather than throw.
  [[nodiscard]] nlohmann::json getPrefabBody(const uuids::uuid& uuid) const;

  // Monotonically incremented by registerAsset and clear. Consumers can cache
  // derived views and invalidate when this value changes.
  [[nodiscard]] size_t getVersion() const;

  [[nodiscard]] nlohmann::json serialize() const;

  void loadFromJSON(const nlohmann::json& assetsData);

  // Binary equivalents of serialize()/loadFromJSON for the network snapshot. Like serialize(), pack()
  // only writes the flat file assets (Model/Texture/Script/Prefab); Scene records belong to the
  // SceneManager.
  void pack(net::Message& message) const;

  void unpack(net::MessageReader& messageReader);

private:
  std::unordered_map<uuids::uuid, AssetRecord> m_assets;

  std::unordered_map<std::string, uuids::uuid> m_loadedPaths;

  size_t m_version = 0;
};



#endif //ASSETREGISTRY_H
