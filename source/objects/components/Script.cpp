#include "Script.h"
#include "../Object.h"
#include "../../ECS3D.h"
#include "../../scripts/ScriptManager.h"
#include <nlohmann/json.hpp>

Script::Script(std::string className)
  : Component(ComponentType::script), m_className(std::move(className))
{}

void Script::start()
{
  Component::start();

  if (!m_scriptManager)
  {
    m_scriptManager = m_owner->getManager()->getECS()->getScriptManager();
  }

  m_scriptManager->attachScript(m_owner->getUUID(), m_className.c_str());
}

void Script::stop()
{
  Component::stop();

  if (!m_scriptManager)
  {
    m_scriptManager = m_owner->getManager()->getECS()->getScriptManager();
  }

  m_scriptManager->detachScript(m_owner->getUUID(), m_className.c_str());
}

void Script::variableUpdate()
{
  if (!m_scriptManager)
  {
    m_scriptManager = m_owner->getManager()->getECS()->getScriptManager();
  }

  m_scriptManager->variableUpdate(m_owner->getUUID(), m_className.c_str());
}

void Script::fixedUpdate(const float dt)
{
  if (!m_scriptManager)
  {
    m_scriptManager = m_owner->getManager()->getECS()->getScriptManager();
  }

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
