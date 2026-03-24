#include "SceneAsset.h"
#include "AssetManager.h"
#include "../objects/ObjectManager.h"
#include "../scenes/SceneManager.h"
#include <imgui.h>
#include <nlohmann/json.hpp>

SceneAsset::SceneAsset(const uuids::uuid uuid,
                       std::string name)
  : Asset(AssetType::Scene, uuid, std::move(name))
{}

void SceneAsset::fixedUpdate(const float dt) const
{
  if (m_sceneManager->getSceneStatus() != SceneStatus::running)
  {
    return;
  }

  m_objectManager->fixedUpdate(dt);
}

void SceneAsset::variableUpdate() const
{
  m_objectManager->variableUpdate();
}

void SceneAsset::start() const
{
  m_objectManager->start();
}

void SceneAsset::stop() const
{
  m_objectManager->stop();
}

void SceneAsset::load()
{
  const auto ecs = m_assetManager->getECS();

  m_sceneManager = ecs->getSceneManager();

  m_objectManager = std::make_shared<ObjectManager>(ecs);
}

void SceneAsset::displayGui(float cellSize)
{
  Asset::displayGui(cellSize);

  ImGui::Button("Scene", { cellSize, cellSize });
}

nlohmann::json SceneAsset::serialize()
{
  const auto serializedObjects = m_objectManager->serialize();
  nlohmann::json data = {
    { "name", m_name },
    { "objects", serializedObjects["objects"] },
    { "uuid", uuids::to_string(m_uuid) }
  };

  return data;
}

std::shared_ptr<ObjectManager> SceneAsset::getObjectManager() const
{
  return m_objectManager;
}
