#include "Script.h"
#include <utility>

Script::Script()
  : Component(ComponentType::script)
{}

Script::Script(std::string className)
  : Component(ComponentType::script), m_className(std::move(className))
{}

std::string Script::getClassName() const
{
  return m_className;
}

void Script::setClassName(const std::string& className)
{
  m_className = className;
}

const nlohmann::json& Script::getFields() const
{
  return m_fields;
}

void Script::setFields(const nlohmann::json& fields)
{
  m_fields = fields;
}

nlohmann::json Script::serialize()
{
  nlohmann::json data = {
    { "type", "Script" },
    { "className", m_className },
    { "fields", m_fields }
  };

  return data;
}

void Script::loadFromJSON(const nlohmann::json& componentData)
{
  if (componentData.contains("className"))
  {
    m_className = componentData.at("className");
  }

  if (componentData.contains("fields"))
  {
    m_fields = componentData.at("fields");
  }
}
