#include "ComponentRegistry.h"

void ComponentRegistry::registerComponent(const std::string& typeName, Factory factory)
{
  m_factories[typeName] = std::move(factory);
}

std::shared_ptr<Component> ComponentRegistry::create(const std::string& typeName) const
{
  const auto it = m_factories.find(typeName);

  return it != m_factories.end() ? it->second() : nullptr;
}

bool ComponentRegistry::isRegistered(const std::string& typeName) const
{
  return m_factories.contains(typeName);
}
