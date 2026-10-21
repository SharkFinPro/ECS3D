#ifndef SCRIPTCLASSASSETS_H
#define SCRIPTCLASSASSETS_H

#include "AssetRegistry.h"
#include <cstddef>
#include <string>
#include <vector>
#include <uuid.h>

// A compiled script class. A scene script and an object script cannot attach to each other's host.
struct ScriptClassInfo {
  std::string name;
  bool sceneScript = false;
};

// A record from a project file has kind Either, which counts as usable on both hosts.
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

// Registers a Script record for each class no Script record names yet, noting its kind. The record
// has no source path, since the compiler does not say which file a class came from. Returns how many were added.
size_t registerScriptClassAssets(AssetRegistry& registry, const std::vector<ScriptClassInfo>& classes);

// A record with no path comes from a compiled class rather than a file the user can remove.
[[nodiscard]] inline bool isCompiledScriptRecord(const AssetRecord& record)
{
  return record.type == AssetType::Script && record.path.empty();
}

#endif //SCRIPTCLASSASSETS_H
