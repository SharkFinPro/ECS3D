#include "Object.h"
#include "../ComponentRegistry.h"
#include "components/Component.h"
#include "components/Transform.h"
#include <nlohmann/json.hpp>
#include <glm/vec3.hpp>
#include <stdexcept>
#include <utility>

Object::Object(std::string name)
  : m_name(std::move(name))
{
  addComponent(std::make_shared<Transform>(glm::vec3(0), glm::vec3(1), glm::vec3(0)));
}

Object::Object(const std::vector<std::shared_ptr<Component>>& components, std::string name)
  : m_name(std::move(name))
{
  for (const auto& component : components)
  {
    addComponent(component);
  }
}

Object::Object(const nlohmann::json& objectData,
               ObjectManager* manager)
  : m_manager(manager)
{
  loadFromJSON(objectData);
}

void Object::loadChildren(const nlohmann::json& childrenData)
{
  for (const auto& childData : childrenData)
  {
    const auto child = std::make_shared<Object>(childData, m_manager);
    child->setParent(shared_from_this());

    m_manager->addObject(child);

    if (childData.contains("children"))
    {
      child->loadChildren(childData["children"]);
    }
  }
}

void Object::setParent(const std::shared_ptr<Object>& parent)
{
  m_parent = parent;
}

std::shared_ptr<Object> Object::getParent() const
{
  return m_parent.lock();
}

void Object::addChild(std::shared_ptr<Object> child)
{
  m_children.emplace_back(std::move(child));
}

void Object::removeChild(const std::shared_ptr<Object>& child)
{
  std::erase(m_children, child);
}

const std::vector<std::shared_ptr<Object>>& Object::getChildren() const
{
  return m_children;
}

void Object::addComponent(const std::shared_ptr<Component>& component,
                          const bool setOwner)
{
  if (component->getType() == ComponentType::script)
  {
    // TODO: dedupe by class name (a Script is only added once) like the old addComponent did. That
    // TODO:   needs Script migrated to ECS3DData so we can read its className without a back-include.
    m_scripts.push_back(component);
  }
  else
  {
    m_components.emplace(component->getType(), component);
  }

  if (setOwner)
  {
    component->setOwner(this);
  }
}

void Object::setManager(ObjectManager* objectManager)
{
  m_manager = objectManager;

  if (m_uuid.is_nil())
  {
    m_uuid = m_manager->createUUID();
  }
}

ObjectManager* Object::getManager() const
{
  return m_manager;
}

std::string Object::getName() const
{
  return m_name;
}

void Object::setName(const std::string& name)
{
  m_name = name;
}

void Object::start() const
{
  for (const auto& [type, component] : m_components)
  {
    component->start();
  }

  for (const auto& script : m_scripts)
  {
    script->start();
  }
}

void Object::stop() const
{
  for (const auto& [type, component] : m_components)
  {
    component->stop();
  }

  for (const auto& script : m_scripts)
  {
    script->stop();
  }
}

nlohmann::json Object::serialize()
{
  std::string cleanName = m_name;
  cleanName.erase(std::ranges::find(cleanName, '\0'), cleanName.end());

  nlohmann::json data = {
    { "name", cleanName },
    { "children", nlohmann::json::array() },
    { "components", nlohmann::json::array() },
    { "scripts", nlohmann::json::array() },
    { "uuid", uuids::to_string(m_uuid) }
  };

  for (const auto& [_, component] : m_components)
  {
    data["components"].push_back(component->serialize());
  }

  for (const auto& script : m_scripts)
  {
    data["scripts"].push_back(script->serialize());
  }

  for (const auto& child : m_children)
  {
    data["children"].push_back(child->serialize());
  }

  return data;
}

uuids::uuid Object::getUUID() const
{
  return m_uuid;
}

bool Object::isAncestorOf(const std::shared_ptr<Object>& object) const
{
  auto current = getParent();
  while (current)
  {
    if (object == current)
    {
      return true;
    }

    current = current->getParent();
  }

  return false;
}

const std::unordered_map<ComponentType, std::shared_ptr<Component>>& Object::getComponents() const
{
  return m_components;
}

const std::vector<std::shared_ptr<Component>>& Object::getScripts() const
{
  return m_scripts;
}

std::shared_ptr<Component> Object::getComponent(const ComponentType type) const
{
  const auto component = m_components.find(type);

  if (component == m_components.end())
  {
    if (const auto parent = getParent())
    {
      if (type == ComponentType::rigidBody)
      {
        return parent->getComponent(type);
      }
    }

    return nullptr;
  }

  return component->second;
}

void Object::loadFromJSON(const nlohmann::json& objectData)
{
  m_uuid = uuids::uuid::from_string(std::string(objectData.at("uuid"))).value();
  m_name = objectData.at("name");

  const auto& registry = m_manager->getComponentRegistry();

  for (const auto& componentData : objectData["components"])
  {
    const auto componentType = componentData.at("type").get<std::string>();

    // A Collider serializes as type "Collider" with a "subType" (Box/Sphere); colliders are
    // registered under that subType, so resolve the registry key accordingly.
    const auto registryKey = componentType == "Collider"
      ? componentData.at("subType").get<std::string>()
      : componentType;

    const auto component = registry->create(registryKey);

    if (!component)
    {
      throw std::runtime_error("Unknown component type: " + registryKey);
    }

    addComponent(component);
    component->loadFromJSON(componentData);
  }

  // TODO: scripts need ECS3DScripting. The registry's "Script" factory must inject the server's
  // TODO:   ScriptManager, and Script (className) must migrate to ECS3DData. Until then, scripts on a
  // TODO:   loaded scene are skipped here (old code did make_shared<Script>(className, scriptManager)).
}
