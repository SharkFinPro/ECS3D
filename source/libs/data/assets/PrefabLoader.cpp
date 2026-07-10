#include "PrefabLoader.h"
#include "AssetRegistry.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace prefabs {

bool saveBody(const std::string& path, const nlohmann::json& body)
{
  try
  {
    const std::filesystem::path filePath(path);
    if (filePath.has_parent_path())
    {
      std::filesystem::create_directories(filePath.parent_path());
    }

    std::ofstream out(filePath);
    if (!out.is_open())
    {
      std::cerr << "[PrefabLoader] Could not write prefab: " << path << std::endl;
      return false;
    }

    out << body.dump(2);
    return out.good();
  }
  catch (const std::exception& e)
  {
    std::cerr << "[PrefabLoader] Failed to write prefab '" << path << "': " << e.what() << std::endl;
    return false;
  }
}

nlohmann::json loadBody(const AssetRegistry& assetRegistry, const uuids::uuid& prefabUUID)
{
  const auto* record = assetRegistry.getByUUID(prefabUUID);
  if (!record || record->type != AssetType::Prefab)
  {
    std::cerr << "[PrefabLoader] No prefab asset registered for " << uuids::to_string(prefabUUID) << std::endl;
    return nullptr;
  }

  std::ifstream file(record->path);
  if (!file.is_open())
  {
    // The path is relative to the working directory (the exe dir), which is the usual culprit.
    std::cerr << "[PrefabLoader] Could not open prefab file: " << record->path
              << " (cwd: " << std::filesystem::current_path().string() << ")" << std::endl;
    return nullptr;
  }

  auto body = nlohmann::json::parse(file, nullptr, false);
  if (body.is_discarded() || !body.is_object())
  {
    std::cerr << "[PrefabLoader] Malformed prefab file: " << record->path << std::endl;
    return nullptr;
  }

  return body;
}

}
