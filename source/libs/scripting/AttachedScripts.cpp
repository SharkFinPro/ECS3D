#include "AttachedScripts.h"

bool AttachedScripts::attach(const uuids::uuid& uuid, const std::string& className)
{
  return m_classesByUUID[uuid].insert(className).second;
}

bool AttachedScripts::detach(const uuids::uuid& uuid, const std::string& className)
{
  const auto it = m_classesByUUID.find(uuid);
  if (it == m_classesByUUID.end() || it->second.erase(className) == 0)
  {
    return false;
  }

  if (it->second.empty())
  {
    m_classesByUUID.erase(it);
  }

  return true;
}

void AttachedScripts::clear()
{
  m_classesByUUID.clear();
}

bool AttachedScripts::isAttached(const uuids::uuid& uuid, const std::string& className) const
{
  const auto it = m_classesByUUID.find(uuid);
  return it != m_classesByUUID.end() && it->second.contains(className);
}

bool AttachedScripts::hasAnyAttached(const uuids::uuid& uuid) const
{
  return m_classesByUUID.contains(uuid);
}

bool AttachedScripts::empty() const
{
  return m_classesByUUID.empty();
}
