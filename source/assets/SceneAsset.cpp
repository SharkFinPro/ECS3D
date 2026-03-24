#include "SceneAsset.h"
#include "AssetManager.h"
#include "../objects/ObjectManager.h"
#include "../scenes/SceneManager.h"
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
  m_sceneManager = m_assetManager->getECS()->getSceneManager();
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
