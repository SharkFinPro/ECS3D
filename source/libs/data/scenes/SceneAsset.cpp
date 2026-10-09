#include "SceneAsset.h"
#include "../objects/Object.h"
#include "../objects/ObjectManager.h"
#include "../objects/components/Script.h"
#include <nlohmann/json.hpp>
#include <Log.h>
#include <Protocol.h>
#include <algorithm>
#include <stdexcept>
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

void SceneAsset::loadScripts(const nlohmann::json& scriptsData)
{
  m_scripts.clear();

  if (!scriptsData.is_array())
  {
    if (!scriptsData.is_null())
    {
      Log::warn(LogCategory::assets, "scene '" + m_name + "': 'scripts' is not an array, ignoring it");
    }

    return;
  }

  for (const auto& scriptData : scriptsData)
  {
    if (!scriptData.is_object() || !scriptData.contains("className") || !scriptData.at("className").is_string()
        || scriptData.at("className").get<std::string>().empty())
    {
      Log::warn(LogCategory::assets, "scene '" + m_name + "': skipping a scene script with no class name");
      continue;
    }

    auto script = std::make_shared<Script>();
    script->loadFromJSON(scriptData);

    if (!addScript(script))
    {
      Log::warn(LogCategory::assets,
                "scene '" + m_name + "': skipping a repeated scene script '" + script->getClassName() + "'");
    }
  }
}

const std::vector<std::shared_ptr<Script>>& SceneAsset::getScripts() const
{
  return m_scripts;
}

std::shared_ptr<Script> SceneAsset::findScript(const std::string& className) const
{
  const auto it = std::ranges::find_if(m_scripts, [&className](const std::shared_ptr<Script>& script) {
    return script->getClassName() == className;
  });

  return it != m_scripts.end() ? *it : nullptr;
}

bool SceneAsset::addScript(const std::shared_ptr<Script>& script, const std::optional<std::size_t> index)
{
  if (!script || script->getClassName().empty() || findScript(script->getClassName()))
  {
    return false;
  }

  const std::size_t position = index ? std::min(*index, m_scripts.size()) : m_scripts.size();
  m_scripts.insert(m_scripts.begin() + static_cast<std::ptrdiff_t>(position), script);

  return true;
}

bool SceneAsset::removeScript(const std::string& className)
{
  return std::erase_if(m_scripts, [&className](const std::shared_ptr<Script>& script) {
    return script->getClassName() == className;
  }) > 0;
}

bool SceneAsset::moveScript(const std::string& className, const std::size_t index)
{
  const auto it = std::ranges::find_if(m_scripts, [&className](const std::shared_ptr<Script>& script) {
    return script->getClassName() == className;
  });

  if (it == m_scripts.end())
  {
    return false;
  }

  const auto currentIndex = static_cast<std::size_t>(it - m_scripts.begin());

  if (index > m_scripts.size() - 1 || index == currentIndex)
  {
    return false;
  }

  const auto script = *it;
  m_scripts.erase(it);
  m_scripts.insert(m_scripts.begin() + static_cast<std::ptrdiff_t>(index), script);

  return true;
}

nlohmann::json SceneAsset::serializeScripts() const
{
  auto scripts = nlohmann::json::array();

  for (const auto& script : m_scripts)
  {
    scripts.push_back(script->serialize());
  }

  return scripts;
}

void SceneAsset::start()
{
  // Captured before anything runs, so a spawn/destroy/reparent during the run can be undone on stop the
  // same way a component value already is.
  m_authoredObjects = m_objectManager->serialize().at("objects");

  m_authoredScripts = serializeScripts();

  m_objectManager->start();
}

void SceneAsset::stop()
{
  m_objectManager->stop();

  if (!m_authoredObjects.is_null())
  {
    m_objectManager->restoreFromJSON(m_authoredObjects);
    m_authoredObjects = nullptr;
  }

  if (!m_authoredScripts.is_null())
  {
    loadScripts(m_authoredScripts);
    m_authoredScripts = nullptr;
  }
}

nlohmann::json SceneAsset::serialize() const
{
  const auto serializedObjects = m_objectManager->serialize();

  nlohmann::json data = {
    { "name", m_name },
    { "objects", serializedObjects["objects"] },
    { "scripts", serializeScripts() },
    { "uuid", uuids::to_string(m_uuid) }
  };

  return data;
}

void SceneAsset::pack(net::Message& message) const
{
  message.writeString(uuids::to_string(m_uuid));
  message.writeString(m_name);

  m_objectManager->pack(message);

  message.write(static_cast<uint32_t>(m_scripts.size()));
  for (const auto& script : m_scripts)
  {
    script->pack(message);
  }
}

std::shared_ptr<SceneAsset> SceneAsset::unpack(net::MessageReader& messageReader,
                                               const std::shared_ptr<ComponentRegistry>& componentRegistry)
{
  const auto uuid = uuids::uuid::from_string(messageReader.readString()).value();
  const auto name = messageReader.readString();

  auto scene = std::make_shared<SceneAsset>(uuid, name, componentRegistry);
  scene->m_objectManager->unpack(messageReader);

  // The count comes off the wire, so it sizes nothing before the bytes behind it are read.
  const auto scriptCount = messageReader.read<uint32_t>();
  for (uint32_t i = 0; i < scriptCount; ++i)
  {
    if (messageReader.read<ComponentType>() != ComponentType::script)
    {
      throw std::runtime_error("Scene script section carries a type tag that is not a script");
    }

    const auto script = std::make_shared<Script>(messageReader.readString());
    script->unpack(messageReader);

    if (!scene->addScript(script))
    {
      Log::warn(LogCategory::assets, "scene '" + name + "': skipping a repeated or unnamed scene script on the wire");
    }
  }

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

void SceneAsset::setName(std::string name)
{
  m_name = std::move(name);
}
