#include "UnresolvedScripts.h"
#include <objects/components/Component.h>
#include <iterator>

bool UnresolvedScripts::add(const uuids::uuid& uuid,
                            const std::string& className,
                            const std::shared_ptr<const Component>& component)
{
  auto& classes = m_byUUID[uuid];
  const auto it = classes.find(className);
  if (it != classes.end() && it->second.lock() == component)
  {
    return false;
  }

  classes[className] = component;
  return true;
}

bool UnresolvedScripts::isUnresolved(const uuids::uuid& uuid,
                                     const std::string& className,
                                     const Component* component) const
{
  const auto byUUID = m_byUUID.find(uuid);
  if (byUUID == m_byUUID.end())
  {
    return false;
  }

  const auto it = byUUID->second.find(className);
  return it != byUUID->second.end() && component != nullptr && it->second.lock().get() == component;
}

void UnresolvedScripts::prune(const LiveLookup& live)
{
  for (auto byUUID = m_byUUID.begin(); byUUID != m_byUUID.end();)
  {
    auto& classes = byUUID->second;
    for (auto it = classes.begin(); it != classes.end();)
    {
      const auto recorded = it->second.lock();
      if (!recorded || live(byUUID->first, it->first) != recorded.get())
      {
        it = classes.erase(it);
      }
      else
      {
        ++it;
      }
    }

    byUUID = classes.empty() ? m_byUUID.erase(byUUID) : std::next(byUUID);
  }
}

void UnresolvedScripts::clear()
{
  m_byUUID.clear();
}

bool UnresolvedScripts::empty() const
{
  return m_byUUID.empty();
}
