#ifndef SCRIPTCLASSASSETS_H
#define SCRIPTCLASSASSETS_H

#include "AssetRegistry.h"
#include <optional>
#include <string>
#include <vector>
#include <uuid.h>

// A compiled script class. A scene script and an object script cannot attach to each other's host.
struct ScriptClassInfo {
  std::string name;
  bool sceneScript = false;
};

// A record whose class is not compiled has kind Either, which counts as usable on both hosts.
[[nodiscard]] inline bool usableOnObject(const AssetRecord& record)
{
  return record.type == AssetType::Script && record.scriptKind != ScriptKind::Scene;
}

[[nodiscard]] inline bool usableOnScene(const AssetRecord& record)
{
  return record.type == AssetType::Script && record.scriptKind != ScriptKind::Object;
}

// The uuid every machine derives for a script class of this name, so a record registered here is the same
// across runs and servers.
[[nodiscard]] uuids::uuid scriptClassUuid(const std::string& className);

// Brings the Script records in line with the compile; nullopt means no compile has succeeded yet, so nothing
// is known and nothing changes. Every record of a compiled class takes the compile's kind. A compiled class
// with no record gets a pathless one, since the compiler does not say which file a class came from. A pathless
// record whose class no longer compiles is removed, and a file-backed one goes back to Either.
// Returns true when anything changed.
bool syncScriptClassAssets(AssetRegistry& registry, const std::optional<std::vector<ScriptClassInfo>>& compiled);

// A class that still compiles comes back at the next sync, so its record cannot be deleted from the editor.
[[nodiscard]] inline bool canDeleteAsset(const AssetRecord& record)
{
  return record.type != AssetType::Script || record.scriptKind == ScriptKind::Either;
}

#endif //SCRIPTCLASSASSETS_H
