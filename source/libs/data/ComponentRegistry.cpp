#include "ComponentRegistry.h"

void ComponentRegistry::registerComponent(const std::string& typeName, Factory factory)
{
  // TODO: store the factory under typeName so deserialization can look it up by string.
  // TODO:   ECS3DData registers every component it owns here, replacing the type switch
  // TODO:   currently in Object::loadComponentFromJSON.
  m_factories[typeName] = std::move(factory);
}

std::shared_ptr<Component> ComponentRegistry::create(const std::string& typeName) const
{
  // TODO: look up the factory for typeName and invoke it; return nullptr / log if unknown.
  const auto it = m_factories.find(typeName);

  return it != m_factories.end() ? it->second() : nullptr;
}

bool ComponentRegistry::isRegistered(const std::string& typeName) const
{
  return m_factories.contains(typeName);
}
