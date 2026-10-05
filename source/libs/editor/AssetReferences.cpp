#include "AssetReferences.h"
#include <assets/AssetRegistry.h>
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <scenes/SceneAsset.h>
#include <scenes/SceneManager.h>
#include <nlohmann/json.hpp>

bool objectReferencesAsset(const std::shared_ptr<Object>& object, const std::string& uuidString)
{
  for (const auto& [type, component] : object->getComponents())
  {
    if (component->serialize().dump().find(uuidString) != std::string::npos)
    {
      return true;
    }
  }

  return false;
}

int countReferencesInPrefabNode(const nlohmann::json& node, const std::string& uuidString)
{
  int count = 0;

  if (const auto components = node.find("components"); components != node.end() && components->is_array())
  {
    for (const auto& component : *components)
    {
      if (component.dump().find(uuidString) != std::string::npos)
      {
        ++count;
        break;
      }
    }
  }

  if (const auto children = node.find("children"); children != node.end() && children->is_array())
  {
    for (const auto& child : *children)
    {
      if (child.is_object())
      {
        count += countReferencesInPrefabNode(child, uuidString);
      }
    }
  }

  return count;
}

int countAssetReferences(const SceneManager& sceneManager, const AssetRegistry& assetRegistry,
                         const uuids::uuid& assetUUID)
{
  const auto uuidString = uuids::to_string(assetUUID);
  int count = 0;

  for (const auto& [sceneUUID, scene] : sceneManager.getScenes())
  {
    const auto objectManager = scene->getObjectManager();
    if (!objectManager)
    {
      continue;
    }

    for (const auto& object : objectManager->getAllObjects())
    {
      if (objectReferencesAsset(object, uuidString))
      {
        ++count;
      }
    }
  }

  for (const auto& [recordUUID, record] : assetRegistry.getAssets())
  {
    if (record.type != AssetType::Prefab)
    {
      continue;
    }

    if (auto body = nlohmann::json::parse(record.body, nullptr, false); !body.is_discarded() && body.is_object())
    {
      count += countReferencesInPrefabNode(body, uuidString);
    }
  }

  return count;
}
