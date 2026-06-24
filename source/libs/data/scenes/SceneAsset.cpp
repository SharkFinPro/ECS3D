#include "SceneAsset.h"
#include "../objects/Object.h"
#include "../objects/ObjectManager.h"
#include <nlohmann/json.hpp>
#include <utility>

SceneAsset::SceneAsset(const uuids::uuid uuid,
                       std::string name,
                       const std::shared_ptr<ComponentRegistry>& componentRegistry)
  : m_uuid(uuid),
    m_name(std::move(name)),
    m_objectManager(std::make_shared<ObjectManager>(componentRegistry))
{}

void SceneAsset::loadObjects(const nlohmann::json& objectsData) const
{
  // Was AssetManager::loadScenesFromJSON's inner loop.
  for (const auto& objectData : objectsData)
  {
    auto object = std::make_shared<Object>(objectData, m_objectManager.get());
    m_objectManager->addObject(object);

    if (objectData.contains("children"))
    {
      object->loadChildren(objectData.at("children"));
    }
  }
}

void SceneAsset::start() const
{
  m_objectManager->start();
}

void SceneAsset::stop() const
{
  m_objectManager->stop();
}

nlohmann::json SceneAsset::serialize() const
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

uuids::uuid SceneAsset::getUUID() const
{
  return m_uuid;
}

std::string SceneAsset::getName() const
{
  return m_name;
}
