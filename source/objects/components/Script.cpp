#include "Script.h"
#include "../Object.h"
#include "../../ECS3D.h"
#include "../../scripts/ScriptManager.h"
#include <imgui.h>
#include <nlohmann/json.hpp>

Script::Script(std::string className)
  : Component(ComponentType::script), m_className(std::move(className))
{}

Script::~Script()
{
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

  if (!m_scriptManager->isScriptAttached(m_owner->getUUID(), m_className.c_str()))
  {
    m_scriptManager->attachScript(m_owner->getUUID(), m_className.c_str());
  }

  m_scriptManager->variableUpdate(m_owner->getUUID(), m_className.c_str());
}

void Script::fixedUpdate(const float dt)
{
  if (!m_scriptManager)
  {
    m_scriptManager = m_owner->getManager()->getECS()->getScriptManager();
  }

  if (!m_scriptManager->isScriptAttached(m_owner->getUUID(), m_className.c_str()))
  {
    m_scriptManager->attachScript(m_owner->getUUID(), m_className.c_str());
  }

  m_scriptManager->fixedUpdate(m_owner->getUUID(), m_className.c_str(), dt);
}

void Script::displayGui()
{
  if (displayGuiHeader())
  {
    const auto fields = m_scriptManager->getExposedFields(m_owner->getUUID(), m_className.c_str());
    if (!fields)
    {
      return;
    }

    for (const auto& field : *fields)
    {
      const auto uuid      = m_owner->getUUID();
      const auto className = m_className.c_str();
      const auto fieldName = field.name.c_str();

      if (field.type == "float")
      {
        float v = m_scriptManager->getFieldFloat(uuid, className, fieldName);
        if (ImGui::InputFloat(field.displayName.c_str(), &v))
        {
          m_scriptManager->setFieldFloat(uuid, className, fieldName, v);
        }
      }
      else if (field.type == "int")
      {
        int v = m_scriptManager->getFieldInt(uuid, className, fieldName);
        if (ImGui::InputInt(field.displayName.c_str(), &v))
        {
          m_scriptManager->setFieldInt(uuid, className, fieldName, v);
        }
      }
      else if (field.type == "bool")
      {
        bool v = m_scriptManager->getFieldBool(uuid, className, fieldName);
        if (ImGui::Checkbox(field.displayName.c_str(), &v))
        {
          m_scriptManager->setFieldBool(uuid, className, fieldName, v);
        }
      }
    }
  }
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
