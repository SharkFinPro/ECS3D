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
    if (!m_scriptManager)
    {
      m_scriptManager = m_owner->getManager()->getECS()->getScriptManager();
    }

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
  nlohmann::json data = {
    { "type", "Script" },
    { "className", m_className },
    { "fields", nlohmann::json::array() }
  };

  if (!m_scriptManager)
  {
    m_scriptManager = m_owner->getManager()->getECS()->getScriptManager();
  }

  if (const auto fields = m_scriptManager->getExposedFields(m_owner->getUUID(), m_className.c_str()))
  {
    for (const auto& field : *fields)
    {
      const auto uuid = m_owner->getUUID();
      const auto className = m_className.c_str();
      const auto fieldName = field.name.c_str();

      if (field.type == "float")
      {
        float v = m_scriptManager->getFieldFloat(uuid, className, fieldName);

        data["fields"].push_back({
          { "name", fieldName },
          { "type", "float" },
          { "value", v }
        });
      }
      else if (field.type == "int")
      {
        int v = m_scriptManager->getFieldInt(uuid, className, fieldName);

        data["fields"].push_back({
          { "name", fieldName },
          { "type", "int" },
          { "value", v }
        });
      }
      else if (field.type == "bool")
      {
        bool v = m_scriptManager->getFieldBool(uuid, className, fieldName);

        data["fields"].push_back({
          { "name", fieldName },
          { "type", "bool" },
          { "value", v }
        });
      }
    }
  }

  return data;
}

void Script::loadFromJSON(const nlohmann::json& componentData)
{
  if (!m_scriptManager)
  {
    m_scriptManager = m_owner->getManager()->getECS()->getScriptManager();
  }

  const auto uuid = m_owner->getUUID();
  const auto className = m_className.c_str();

  if (!m_scriptManager->isScriptAttached(uuid, className))
  {
    m_scriptManager->attachScript(uuid, className);
  }

  if (!componentData.contains("fields"))
  {
    return;
  }

  for (const auto& field : componentData["fields"])
  {
    const std::string name = field["name"];
    const std::string type = field["type"];

    const auto fieldName = name.c_str();

    if (type == "float")
    {
      const float v = field["value"];
      m_scriptManager->setFieldFloat(uuid, className, fieldName, v);
    }
    else if (type == "int")
    {
      const int v = field["value"];
      m_scriptManager->setFieldInt(uuid, className, fieldName, v);
    }
    else if (type == "bool")
    {
      const bool v = field["value"];
      m_scriptManager->setFieldBool(uuid, className, fieldName, v);
    }
  }
}
