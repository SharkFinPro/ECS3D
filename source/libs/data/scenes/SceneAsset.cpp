#include "SceneAsset.h"
#include "../objects/Object.h"
#include "../objects/ObjectManager.h"
#include <nlohmann/json.hpp>
#include <Protocol.h>
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

void SceneAsset::pack(net::Message& message) const
{
  message.writeString(uuids::to_string(m_uuid));
  message.writeString(m_name);

  m_objectManager->pack(message);
}

std::shared_ptr<SceneAsset> SceneAsset::unpack(net::MessageReader& messageReader,
                                               const std::shared_ptr<ComponentRegistry>& componentRegistry)
{
  const auto uuid = uuids::uuid::from_string(messageReader.readString()).value();
  const auto name = messageReader.readString();

  auto scene = std::make_shared<SceneAsset>(uuid, name, componentRegistry);
  scene->m_objectManager->unpack(messageReader);

  return scene;
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
