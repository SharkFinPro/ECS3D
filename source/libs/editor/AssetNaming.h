#ifndef ASSETNAMING_H
#define ASSETNAMING_H

#include <assets/AssetRegistry.h>
#include <functional>
#include <string>

namespace assetNaming {
  // The key a record of this type is registered under: the name for scenes and prefabs, the class name
  // for scripts.
  [[nodiscard]] inline const std::string& keyOf(const AssetRecord& record)
  {
    return record.type == AssetType::Script ? record.className : record.path;
  }

  [[nodiscard]] inline bool isNameTaken(const AssetRegistry& registry, const AssetType type, const std::string& name)
  {
    for (const auto& [uuid, record] : registry.getAssets())
    {
      if (record.type == type && (keyOf(record) == name || record.displayName == name))
      {
        return true;
      }
    }

    return false;
  }

  // Mirrors SceneManager::uniqueSceneName, so the name offered is the one the server registers.
  [[nodiscard]] inline std::string uniqueSceneName(const AssetRegistry& registry, const std::string& base)
  {
    if (!isNameTaken(registry, AssetType::Scene, base))
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
      if (!isNameTaken(registry, AssetType::Scene, candidate))
      {
        return candidate;
      }
    }
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
