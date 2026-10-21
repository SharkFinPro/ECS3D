#include "ScriptClassAssets.h"
#include "AssetRegistry.h"
#include <unordered_set>

namespace {
  // Fixed forever: changing it changes every derived uuid.
  const uuids::uuid scriptClassNamespace =
    uuids::uuid::from_string("5c0e6a3e-2b7f-4c1d-9a54-8d1f3b6e7a20").value();
}

uuids::uuid scriptClassUuid(const std::string& className)
{
  return uuids::uuid_name_generator(scriptClassNamespace)(className);
}

size_t registerScriptClassAssets(AssetRegistry& registry, const std::vector<ScriptClassInfo>& classes)
{
  std::unordered_set<std::string> known;
  for (const auto& [uuid, record] : registry.getAssets())
  {
    if (record.type == AssetType::Script)
    {
      known.insert(record.className);
    }
  }

  size_t added = 0;
  for (const auto& [className, sceneScript] : classes)
  {
    if (className.empty() || !known.insert(className).second)
    {
      continue;
    }

    const auto uuid = scriptClassUuid(className);
    if (registry.getByUUID(uuid))
    {
      continue;
    }

    registry.registerAsset({ .uuid = uuid, .type = AssetType::Script, .className = className,
                             .scriptKind = sceneScript ? ScriptKind::Scene : ScriptKind::Object });
    ++added;
  }

  return added;
}
