#include "ProjectPacker.h"
#include "assets/AssetRegistry.h"
#include "scenes/SceneManager.h"
#include "scenes/SceneAsset.h"
#include <Protocol.h>
#include <utility>
#include <vector>

ProjectPacker::ProjectPacker(AssetRegistry* assetRegistry,
                             SceneManager* sceneManager,
                             std::shared_ptr<ComponentRegistry> componentRegistry)
  : m_assetRegistry(assetRegistry),
    m_sceneManager(sceneManager),
    m_componentRegistry(std::move(componentRegistry))
{}

void ProjectPacker::pack(net::Message& message) const
{
  // Same shape as ProjectSerializer::serialize: the flat file assets, then every scene's object tree,
  // then the current scene uuid.
  m_assetRegistry->pack(message);

  const auto& scenes = m_sceneManager->getScenes();
  message.write(static_cast<uint32_t>(scenes.size()));
  for (const auto& [uuid, scene] : scenes)
  {
    scene->pack(message);
  }

  const auto currentScene = m_sceneManager->getCurrentScene();
  message.writeString(currentScene ? uuids::to_string(currentScene->getUUID()) : "");
}

void ProjectPacker::unpack(const net::Message& message) const
{
  net::MessageReader reader(message);

  // Atomic load (mirrors ProjectSerializer::deserialize): parse everything that can throw into local
  // instances first, while the live AssetRegistry/SceneManager are untouched.
  AssetRegistry parsedAssets;
  parsedAssets.unpack(reader);

  const uint32_t sceneCount = reader.read<uint32_t>();
  std::vector<std::shared_ptr<SceneAsset>> parsedScenes;
  parsedScenes.reserve(sceneCount);
  for (uint32_t i = 0; i < sceneCount; ++i)
  {
    parsedScenes.push_back(SceneAsset::unpack(reader, m_componentRegistry));
  }

  const std::string currentSceneUUID = reader.readString();

  // Commit phase: nothing below throws, so the swap to the new project is all-or-nothing. A fresh
  // snapshot must REPLACE the current state (the registries are keyed and won't overwrite), so clear
  // first.
  m_sceneManager->clear();
  *m_assetRegistry = std::move(parsedAssets);

  for (const auto& scene : parsedScenes)
  {
    m_sceneManager->addScene(scene);

    // Register the scene as an asset too (matches deserialize), so it shows up in the editor's asset
    // browser. AssetRegistry::pack ignores Scene records, so this doesn't double-pack.
    m_assetRegistry->registerAsset({ .uuid = scene->getUUID(), .type = AssetType::Scene, .path = scene->getName() });
  }

  if (!currentSceneUUID.empty())
  {
    if (const auto parsed = uuids::uuid::from_string(currentSceneUUID); parsed.has_value())
    {
      if (const auto scene = m_sceneManager->getScene(parsed.value()))
      {
        m_sceneManager->loadScene(scene);
      }
    }
  }
}
