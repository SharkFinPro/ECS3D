#ifndef PREFABLOADER_H
#define PREFABLOADER_H

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <uuid.h>

class AssetRegistry;

// A prefab is a reusable object template. Its AssetRegistry record carries only a file path (exactly
// like a model); the file body is one Object::serialize() blob, so instantiating a prefab is the same
// code path as duplicating an object (ObjectManager::instantiate).
//
// The body is deliberately NOT carried in the snapshot. The authoritative server reads it from disk when
// it instantiates, so the file must exist relative to the server's working directory (the exe dir) — the
// same constraint model/texture paths already live with.
namespace prefabs {

// Where the editor writes new prefabs, relative to the working directory (mirrors assets/models).
inline constexpr const char* directory = "assets/prefabs";
inline constexpr const char* extension = ".prefab";

// Write a serialized object to path, creating parent directories. False on any IO failure.
bool saveBody(const std::string& path, const nlohmann::json& body);

// Read a prefab's body by asset uuid. Returns a null json (and warns) when the uuid isn't a registered
// Prefab, the file is missing, or the JSON is malformed — callers skip the spawn rather than throw.
[[nodiscard]] nlohmann::json loadBody(const AssetRegistry& assetRegistry, const uuids::uuid& prefabUUID);

}



#endif //PREFABLOADER_H
