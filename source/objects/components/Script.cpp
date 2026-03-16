#include "Script.h"
#include "../Object.h"
#include "../../ECS3D.h"
#include "../../scripts/ScriptManager.h"
#include <nlohmann/json.hpp>

Script::Script(std::string className)
  : Component(ComponentType::script), m_className(std::move(className))
{
  m_scriptManager = m_owner->getManager()->getECS()->getScriptManager();
}

void Script::start() const
{
  Component::start();

  m_scriptManager->attachScript(m_owner->getUUID(), m_className.c_str());
}

void Script::stop() const
{
  Component::stop();

  m_scriptManager->attachScript(m_owner->getUUID(), m_className.c_str());
}

void Script::variableUpdate()
{
  m_scriptManager->variableUpdate(m_owner->getUUID(), m_className.c_str());
}

void Script::fixedUpdate(float dt)
{
  m_scriptManager->fixedUpdate(m_owner->getUUID(), m_className.c_str(), dt);
}

void Script::displayGui()
{
  // TODO
}

nlohmann::json Script::serialize()
{
  // TODO

  return {};
}

void Script::loadFromJSON(const nlohmann::json& componentData)
{
  // TODO
}
