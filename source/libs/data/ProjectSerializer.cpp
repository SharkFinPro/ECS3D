#include "ProjectSerializer.h"
#include "assets/AssetRegistry.h"
#include "scenes/SceneManager.h"
#include "scenes/SceneAsset.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <utility>
#include <vector>

ProjectSerializer::ProjectSerializer(AssetRegistry* assetRegistry,
                                     SceneManager* sceneManager,
                                     std::shared_ptr<ComponentRegistry> componentRegistry)
  : m_assetRegistry(assetRegistry),
    m_sceneManager(sceneManager),
    m_componentRegistry(std::move(componentRegistry))
{}

nlohmann::json ProjectSerializer::serialize() const
{
  // AssetRegistry serializes the flat file assets (models/textures/scripts); scenes carry the object
  // tree and live in the SceneManager, so they are merged into the same "assets" object here.
  // The same blob is the network Snapshot sent to a joining client.
  auto assets = m_assetRegistry->serialize();

  assets["scenes"] = nlohmann::json::array();
  for (const auto& [uuid, scene] : m_sceneManager->getScenes())
  {
    assets["scenes"].push_back(scene->serialize());
  }

  const auto currentScene = m_sceneManager->getCurrentScene();
  const auto currentSceneUUID = currentScene ? uuids::to_string(currentScene->getUUID()) : "";

  return {
    { "assets", assets },
    { "currentSceneUUID", currentSceneUUID }
  };
}

void ProjectSerializer::deserialize(const nlohmann::json& saveData) const
{
  if (saveData.empty())
  {
    return;
  }

  const auto& assets = saveData.at("assets");

  // Atomic load: parse everything that can throw into local instances first, while the live
  // AssetRegistry/SceneManager are untouched. Only once the whole blob has parsed do we clear and
  // commit, so a corrupt file or bad snapshot leaves the current project intact.
  AssetRegistry parsedAssets;
  parsedAssets.loadFromJSON(assets);

  std::vector<std::shared_ptr<SceneAsset>> parsedScenes;
  if (assets.contains("scenes"))
  {
    for (const auto& sceneData : assets.at("scenes"))
    {
      const auto uuid = uuids::uuid::from_string(std::string(sceneData.at("uuid"))).value();
      const std::string name = sceneData.at("name");

      const auto scene = std::make_shared<SceneAsset>(uuid, name, m_componentRegistry);
      scene->loadObjects(sceneData.at("objects"));

      parsedScenes.push_back(scene);
    }
  }

  // Commit phase: nothing below throws, so the swap to the new project is all-or-nothing. A fresh
  // snapshot or a different project must REPLACE the current state, not merge with it (addScene/
  // registerAsset are keyed and won't overwrite existing entries), so clear first.
  m_sceneManager->clear();
  *m_assetRegistry = std::move(parsedAssets);

  for (const auto& scene : parsedScenes)
  {
    m_sceneManager->addScene(scene);

    // Register the scene as an asset too, so it shows up in the editor's asset browser (where it can
    // be double-clicked to switch the active scene). The scene DATA stays in the SceneManager; this is
    // just the metadata record (its path holds the display name). AssetRegistry::serialize ignores
    // Scene records, so this doesn't double-serialize.
    m_assetRegistry->registerAsset({ .uuid = scene->getUUID(), .type = AssetType::Scene, .path = scene->getName() });
  }

  if (saveData.contains("currentSceneUUID"))
  {
    const auto currentSceneUUID = std::string(saveData.at("currentSceneUUID"));

    if (const auto parsed = uuids::uuid::from_string(currentSceneUUID); parsed.has_value())
    {
      if (const auto scene = m_sceneManager->getScene(parsed.value()))
      {
        m_sceneManager->loadScene(scene);
      }
    }
  }
}

void ProjectSerializer::save(const std::string& path) const
{
  std::ofstream outFile(path);
  outFile << serialize().dump(2);
}

bool ProjectSerializer::load(const std::string& path) const
{
  std::ifstream f(path);
  if (!f.is_open())
  {
    // Don't fail silently — the server would otherwise just idle with no scene and no clue why. The
    // path is relative to the working directory, which is the usual culprit (run from the bin dir).
    std::cerr << "[ProjectSerializer] Could not open project file: " << path
              << " (cwd: " << std::filesystem::current_path().string() << ")" << std::endl;
    return false;
  }

  try
  {
    deserialize(nlohmann::json::parse(f));
    return true;
  }
  catch (const std::exception& e)
  {
    std::cerr << "[ProjectSerializer] Failed to load project '" << path << "': " << e.what() << std::endl;
    return false;
  }
}
