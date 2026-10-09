#ifndef UNRESOLVEDSCRIPTS_H
#define UNRESOLVEDSCRIPTS_H

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <uuid.h>

class Component;

// The (uuid, class) pairs whose managed instance could not be created, each tied to the Script
// component it was tried for, so a replacement component gets a fresh attempt while the same one is not
// retried (and warned about) every tick. Pure bookkeeping, no CLR.
class UnresolvedScripts {
public:
  using LiveLookup = std::function<const Component*(const uuids::uuid&, const std::string&)>;

  // Returns true when this is a new failure for this component, false when it is already recorded.
  bool add(const uuids::uuid& uuid, const std::string& className, const std::shared_ptr<const Component>& component);

  [[nodiscard]] bool isUnresolved(const uuids::uuid& uuid,
                                  const std::string& className,
                                  const Component* component) const;

  // Drops every entry whose component is no longer the one `live` returns for its pair.
  void prune(const LiveLookup& live);

  void clear();

  [[nodiscard]] bool empty() const;

private:
  std::unordered_map<uuids::uuid, std::unordered_map<std::string, std::weak_ptr<const Component>>> m_byUUID;
};

#endif //UNRESOLVEDSCRIPTS_H
