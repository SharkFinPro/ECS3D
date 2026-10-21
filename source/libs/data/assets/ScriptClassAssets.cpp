#include "ScriptClassAssets.h"
#include "AssetRegistry.h"
#include <unordered_map>
#include <unordered_set>

namespace {
  // Fixed forever: changing it changes every derived uuid.
  const uuids::uuid scriptClassNamespace =
    uuids::uuid::from_string("5c0e6a3e-2b7f-4c1d-9a54-8d1f3b6e7a20").value();

  ScriptKind kindOf(const ScriptClassInfo& info)
  {
    return info.sceneScript ? ScriptKind::Scene : ScriptKind::Object;
  }
}

uuids::uuid scriptClassUuid(const std::string& className)
{
  return uuids::uuid_name_generator(scriptClassNamespace)(className);
}

bool syncScriptClassAssets(AssetRegistry& registry, const std::optional<std::vector<ScriptClassInfo>>& compiled)
{
  if (!compiled)
  {
    return false;
  }

  std::unordered_map<std::string, ScriptKind> kinds;
  for (const auto& info : *compiled)
  {
    if (!info.name.empty())
    {
      kinds.emplace(info.name, kindOf(info));
    }
  }

  bool changed = false;
  std::unordered_set<std::string> known;
  std::vector<uuids::uuid> stale;

  for (const auto& [uuid, record] : registry.getAssets())
  {
    if (record.type != AssetType::Script)
    {
      continue;
    }

    known.insert(record.className);
    if (!kinds.contains(record.className) && record.path.empty())
    {
      stale.push_back(uuid);
    }
  }

  for (const auto& uuid : stale)
  {
    registry.removeAsset(uuid);
    changed = true;
  }

  std::vector<uuids::uuid> scripts;
  for (const auto& [uuid, record] : registry.getAssets())
  {
    if (record.type == AssetType::Script)
    {
      scripts.push_back(uuid);
    }
  }

  for (const auto& uuid : scripts)
  {
    const auto found = kinds.find(registry.getByUUID(uuid)->className);
    changed |= registry.setScriptKind(uuid, found != kinds.end() ? found->second : ScriptKind::Either);
  }

  for (const auto& [name, kind] : kinds)
  {
    if (known.contains(name) || registry.getByUUID(scriptClassUuid(name)))
    {
      continue;
    }

    registry.registerAsset({ .uuid = scriptClassUuid(name), .type = AssetType::Script, .className = name,
                             .scriptKind = kind });
    changed = true;
  }

  return changed;
}
