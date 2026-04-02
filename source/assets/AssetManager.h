#ifndef ASSETMANAGER_H
#define ASSETMANAGER_H

#include "Asset.h"
#include "../ECS3D.h"
#include <nlohmann/json_fwd.hpp>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

class Asset;
class ECS3D;
class SceneAsset;

struct PendingAsset {
  enum class Type { None, Model, Texture, Scene, Script };

  Type type = Type::None;
  std::string path;
  std::string name;

  [[nodiscard]] bool isValid() const
  {
    return type != Type::None && !name.empty();
  }

  [[nodiscard]] bool requiresFilePicker() const
  {
    return type == Type::Model || type == Type::Texture;
  }

  [[nodiscard]] std::string toString() const
  {
    switch (type)
    {
      case Type::Model: return "3D Model";
      case Type::Texture: return "Texture";
      case Type::Scene: return "Scene";
      case Type::Script: return "Script";
      default: return "";
    }
  }
};

enum class SortType {
  NameAscending,
  NameDescending
};

inline std::unordered_map<SortType, std::string> sortTypeToString {
  { SortType::NameAscending, "Name (A-Z)" },
  { SortType::NameDescending, "Name (Z-A)" },
};

class AssetManager {
public:
  explicit AssetManager(ECS3D* ecs);

  [[nodiscard]] ECS3D* getECS() const;

  void displayMenuWidget();

  void displayGui();

  [[nodiscard]] std::shared_ptr<SceneAsset> createSceneAsset(std::string name);

  void loadScriptAsset(const std::string& path,
                       std::string className);

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

  AssetType m_filteredAssetType = AssetType::Unknown;

  std::string m_searchQuery;

  std::vector<std::shared_ptr<Asset>> m_filteredAssets;

  SortType m_sortType = SortType::NameAscending;

  bool m_shouldComputeFilteredAssets = true;

  void displayAssetsFilterGui();

  void computeFilteredAssets();

  void loadModelsFromJSON(const nlohmann::json& assetsData);

  void loadTexturesFromJSON(const nlohmann::json& assetsData);

  void loadScenesFromJSON(const nlohmann::json& assetsData);

  void loadScriptsFromJSON(const nlohmann::json& assetsData);

  void displayCreateAssetPopup(PendingAsset& pending,
                               std::string& errorMessage);

  static void displayPopupSourcePath(const PendingAsset& pending);

  static void displayPopupNameInput(PendingAsset& pending,
                                    std::string& errorMessage);

  void displayPopupActionButtons(PendingAsset& pending,
                                 std::string& errorMessage);

  void commitAssetCreation(const PendingAsset& pending);

  void commitModelAsset(const std::string& name,
                        const std::string& srcPath);

  void commitTextureAsset(const std::string& name,
                          const std::string& srcPath);

  void commitSceneAsset(std::string name);

  void commitScriptAsset(std::string name);
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

  m_shouldComputeFilteredAssets = true;
}

template<typename T>
std::shared_ptr<T> AssetManager::getAsset(const std::string& path) const
{
  const auto uuid = m_loadedPaths.find(path);
  if (uuid == m_loadedPaths.end())
  {
    throw std::runtime_error("Asset not loaded! Path: " + path);
  }

  const auto asset = m_assets.find(uuid->second);

  if (asset == m_assets.end())
  {
    throw std::runtime_error("Asset not found! Path: " + path);
  }

  const auto typedAsset = std::dynamic_pointer_cast<T>(asset->second);
  if (!typedAsset)
  {
    throw std::runtime_error("Asset is not of the requested type! Path: " + path);
  }

  return typedAsset;
}

template<typename T>
std::shared_ptr<T> AssetManager::getAsset(const uuids::uuid uuid) const
{
  const auto asset = m_assets.find(uuid);

  if (asset == m_assets.end())
  {
    throw std::runtime_error("Asset not found! UUID: " + uuids::to_string(uuid));
  }

  const auto typedAsset = std::dynamic_pointer_cast<T>(asset->second);
  if (!typedAsset)
  {
    throw std::runtime_error("Asset is not of the requested type! UUID: " + uuids::to_string(uuid));
  }

  return typedAsset;
}

#endif //ASSETMANAGER_H
