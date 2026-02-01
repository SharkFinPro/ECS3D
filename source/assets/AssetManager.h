#ifndef ASSETMANAGER_H
#define ASSETMANAGER_H

#include "../ECS3D.h"
#include <nlohmann/json_fwd.hpp>
#include <memory>
#include <string>
#include <unordered_map>

class ECS3D;
class Asset;
class TextureAsset;
class ModelAsset;

class AssetManager {
public:
  explicit AssetManager(ECS3D* ecs);

  [[nodiscard]] ECS3D* getECS() const;

  void displayGui();

  template <typename T>
  void loadAsset(const std::string& path);

  template <typename T>
  std::shared_ptr<T> getAsset(const std::string& path) const;

  template <typename T>
  std::shared_ptr<T> getAsset(uuids::uuid uuid) const;

  [[nodiscard]] nlohmann::json serialize();

  void loadFromJSON(const nlohmann::json& assetsData);

private:
  ECS3D* m_ecs;

  std::unordered_map<uuids::uuid, std::shared_ptr<Asset>> m_assets;

  std::unordered_map<std::string, uuids::uuid> m_loadedPaths;
};

template<typename T>
void AssetManager::loadAsset(const std::string& path)
{
  if (m_loadedPaths.contains(path))
  {
    return;
  }

  const uuids::uuid uuid = m_ecs->createUUID();

  const auto asset = std::make_shared<T>(uuid, path);
  asset->setManager(this);
  asset->load();

  m_assets.emplace(uuid, asset);

  m_loadedPaths.emplace(path, uuid);
}

template<typename T>
std::shared_ptr<T> AssetManager::getAsset(const std::string& path) const
{
  const auto uuid = m_loadedPaths.find(path);
  if (uuid == m_loadedPaths.end())
  {
    return nullptr;
  }

  const auto asset = m_assets.find(uuid->second);

  return asset != m_assets.end() ? std::dynamic_pointer_cast<T>(asset->second) : nullptr;
}

template<typename T>
std::shared_ptr<T> AssetManager::getAsset(const uuids::uuid uuid) const
{
  const auto asset = m_assets.find(uuid);

  return asset != m_assets.end() ? std::dynamic_pointer_cast<T>(asset->second) : nullptr;
}

#endif //ASSETMANAGER_H
