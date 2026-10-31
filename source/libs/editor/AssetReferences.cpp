#include "AssetReferences.h"
#include <assets/AssetRegistry.h>
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <scenes/SceneAsset.h>
#include <scenes/SceneManager.h>
#include <nlohmann/json.hpp>

#include <algorithm>

bool objectReferencesAsset(const std::shared_ptr<Object>& object, const std::string& uuidString)
{
  for (const auto& [type, component] : object->getComponents())
  {
    if (component->serialize().dump().find(uuidString) != std::string::npos)
    {
      return true;
    }
  }

  for (const auto& script : object->getScripts())
  {
    if (script->serialize().dump().find(uuidString) != std::string::npos)
    {
      return true;
    }
  }

  return false;
}

int countReferencesInPrefabNode(const nlohmann::json& node, const std::string& uuidString)
{
  int count = 0;

  const auto arrayMentions = [&node, &uuidString](const char* key)
  {
    const auto entries = node.find(key);
    if (entries == node.end() || !entries->is_array())
    {
      return false;
    }

    return std::ranges::any_of(*entries, [&uuidString](const nlohmann::json& entry)
    {
      return entry.dump().find(uuidString) != std::string::npos;
    });
  };

  if (arrayMentions("components") || arrayMentions("scripts"))
  {
    ++count;
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
