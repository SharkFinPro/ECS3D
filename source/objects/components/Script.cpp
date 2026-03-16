#include "Script.h"
#include <nlohmann/json.hpp>

Script::Script(std::string className)
  : Component(ComponentType::script), m_className(std::move(className))
{}

void Script::variableUpdate()
{
  // TODO
}

void Script::fixedUpdate(float dt)
{
  // TODO
}

void Script::displayGui()
{
  // TODO
}

nlohmann::json Script::serialize()
{
  return {};
}

void Script::loadFromJSON(const nlohmann::json& componentData)
{
  // TODO
}
