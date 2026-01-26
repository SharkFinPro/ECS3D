#ifndef ASSETMANAGER_H
#define ASSETMANAGER_H

#include <nlohmann/json_fwd.hpp>
#include <uuid.h>
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

  [[nodiscard]] nlohmann::json serialize();

private:
  ECS3D* m_ecs;

  std::unordered_map<std::string, std::shared_ptr<Asset>> m_assets;

  std::mt19937 m_rng;
  uuids::uuid_random_generator m_uuidGenerator;
};

template<typename T>
void AssetManager::loadAsset(const std::string& path)
{
  if (m_assets.contains(path))
  {
    return;
  }

  const uuids::uuid uuid = m_uuidGenerator();

  const auto asset = std::make_shared<T>(uuid, path);
  asset->setManager(this);
  asset->load();

  m_assets.emplace(path, asset);
}

template<typename T>
std::shared_ptr<T> AssetManager::getAsset(const std::string& path) const
{
  const auto asset = m_assets.find(path);

  return asset != m_assets.end() ? std::dynamic_pointer_cast<T>(asset->second) : nullptr;
}

#endif //ASSETMANAGER_H
