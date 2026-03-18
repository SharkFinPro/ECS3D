#include "Script.h"
#include "../Object.h"
#include "../../scripts/ScriptManager.h"
#include <imgui.h>
#include <nlohmann/json.hpp>

Script::Script(std::string className, std::shared_ptr<ScriptManager> scriptManager)
  : Component(ComponentType::script), m_scriptManager(std::move(scriptManager)), m_className(std::move(className))
{}

Script::~Script()
{
  m_scriptManager->detachScript(m_owner->getUUID(), m_className.c_str());
}

void Script::variableUpdate()
{
  attachScript();

  m_scriptManager->variableUpdate(m_owner->getUUID(), m_className.c_str());
}

void Script::fixedUpdate(const float dt)
{
  attachScript();

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
  nlohmann::json data = {
    { "type", "Script" },
    { "className", m_className },
    { "fields", nlohmann::json::array() }
  };

  if (const auto fields = m_scriptManager->getExposedFields(m_owner->getUUID(), m_className.c_str()))
  {
    for (const auto& field : *fields)
    {
      const auto uuid = m_owner->getUUID();
      const auto className = m_className.c_str();
      const auto fieldName = field.name.c_str();

      data["fields"].push_back({
        { "name", fieldName },
        { "type", field.type },
        { "value", "" }
      });

      if (field.type == "float")
      {
        data["fields"].back()["value"] = m_scriptManager->getFieldFloat(uuid, className, fieldName);
      }
      else if (field.type == "int")
      {
        data["fields"].back()["value"] = m_scriptManager->getFieldInt(uuid, className, fieldName);
      }
      else if (field.type == "bool")
      {
        data["fields"].back()["value"] = m_scriptManager->getFieldBool(uuid, className, fieldName);
      }
    }
  }

  return data;
}

void Script::loadFromJSON(const nlohmann::json& componentData)
{
  const auto uuid = m_owner->getUUID();
  const auto className = m_className.c_str();

  attachScript();

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

void Script::preReload()
{
  m_tempData = serialize();
}

void Script::postReload()
{
  loadFromJSON(m_tempData);

  m_tempData = nlohmann::json();
}

void Script::attachScript()
{
  if (m_scriptManager->isScriptAttached(m_owner->getUUID(), m_className.c_str()))
  {
    return;
  }

  m_scriptManager->attachScript(
    m_owner->getUUID(),
    m_className.c_str(),
    [this] { preReload (); },
    [this] { postReload(); }
  );
}
