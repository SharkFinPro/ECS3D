#ifndef ASSETNAMING_H
#define ASSETNAMING_H

#include <assets/AssetRegistry.h>
#include <assets/ScriptClassAssets.h>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>

namespace assetNaming {
  // The key a record of this type is registered under: the name for scenes and prefabs, the class name
  // for scripts.
  [[nodiscard]] inline const std::string& keyOf(const AssetRecord& record)
  {
    return record.type == AssetType::Script ? record.className : record.path;
  }

  // The registry keys scenes and prefabs by one shared path map, so they share a name space.
  [[nodiscard]] inline bool sharesNameSpace(const AssetType a, const AssetType b)
  {
    const auto named = [](const AssetType t) { return t == AssetType::Scene || t == AssetType::Prefab; };
    return a == b || (named(a) && named(b));
  }

  [[nodiscard]] inline bool isNameTaken(const AssetRegistry& registry, const AssetType type, const std::string& name)
  {
    for (const auto& [uuid, record] : registry.getAssets())
    {
      if (sharesNameSpace(record.type, type) && (keyOf(record) == name || record.displayName == name))
      {
        return true;
      }
    }

    return false;
  }

  // Uses the same " (N)" suffix rule as SceneManager::uniqueSceneName, so the offered name matches the server.
  [[nodiscard]] inline std::string uniqueNameOfType(const AssetRegistry& registry, const AssetType type,
                                                    const std::string& base)
  {
    if (!isNameTaken(registry, type, base))
    {
      return base;
    }

    std::string stem = base;
    if (const auto open = base.rfind(" ("); open != std::string::npos && base.back() == ')')
    {
      const std::string inside = base.substr(open + 2, base.size() - open - 3);
      if (!inside.empty() && inside.find_first_not_of("0123456789") == std::string::npos)
      {
        stem = base.substr(0, open);
      }
    }

    for (size_t n = 2;; ++n)
    {
      std::string candidate = stem + " (" + std::to_string(n) + ")";
      if (!isNameTaken(registry, type, candidate))
      {
        return candidate;
      }
    }
  }

  [[nodiscard]] inline std::string uniqueSceneName(const AssetRegistry& registry, const std::string& base)
  {
    return uniqueNameOfType(registry, AssetType::Scene, base);
  }

  [[nodiscard]] inline std::string uniquePrefabName(const AssetRegistry& registry, const std::string& base)
  {
    return uniqueNameOfType(registry, AssetType::Prefab, base);
  }

  // An Object::serialize() blob for a lone object with the default Transform.
  [[nodiscard]] inline std::string emptyPrefabBody(const std::string& name, const std::string& objectUUID)
  {
    const nlohmann::json body = {
      { "name", name },
      { "children", nlohmann::json::array() },
      { "components", nlohmann::json::array({
        { { "type", "Transform" },
          { "position", { 0.0f, 0.0f, 0.0f } },
          { "rotation", { 0.0f, 0.0f, 0.0f } },
          { "scale", { 1.0f, 1.0f, 1.0f } } }
      }) },
      { "scripts", nlohmann::json::array() },
      { "uuid", objectUUID }
    };

    return body.dump();
  }

  // A C# class name cannot hold a space or parentheses, so the suffix is a bare number.
  [[nodiscard]] inline std::string uniqueScriptName(const AssetRegistry& registry, const std::string& base,
                                                    const std::function<bool(const std::string&)>& fileExists)
  {
    const auto taken = [&](const std::string& name) {
      return isNameTaken(registry, AssetType::Script, name) || (fileExists && fileExists(name));
    };

    if (!taken(base))
    {
      return base;
    }

    for (size_t n = 2;; ++n)
    {
      std::string candidate = base + std::to_string(n);
      if (!taken(candidate))
      {
        return candidate;
      }
    }
  }
}

#endif //ASSETNAMING_H
