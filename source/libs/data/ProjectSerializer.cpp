#include "ProjectSerializer.h"
#include "assets/AssetRegistry.h"
#include "scenes/SceneManager.h"
#include "scenes/SceneAsset.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

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
  // tree and live in the SceneManager, so they are merged into the same "assets" object here (matching
  // the old single-blob save format). The same blob is the network Snapshot sent to a joining client.
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

  // TODO: the old loadFromSaveFile swapped scene+asset managers (prepareForReset/cancelReset) so a
  // TODO:   failed load couldn't corrupt live state. Restore that atomicity at the app level by
  // TODO:   loading into fresh AssetRegistry/SceneManager instances and swapping them in on success.
  const auto& assets = saveData.at("assets");

  m_assetRegistry->loadFromJSON(assets);

  if (assets.contains("scenes"))
  {
    for (const auto& sceneData : assets.at("scenes"))
    {
      const auto uuid = uuids::uuid::from_string(std::string(sceneData.at("uuid"))).value();
      const std::string name = sceneData.at("name");

      const auto scene = std::make_shared<SceneAsset>(uuid, name, m_componentRegistry);
      scene->loadObjects(sceneData.at("objects"));

      m_sceneManager->addScene(scene);
    }
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
