#include "ComponentRegistry.h"

#include <algorithm>

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

std::vector<std::string> ComponentRegistry::registeredNames() const
{
  std::vector<std::string> names;
  names.reserve(m_factories.size());

  for (const auto& [typeName, factory] : m_factories)
  {
    names.push_back(typeName);
  }

  std::ranges::sort(names);

  return names;
}
