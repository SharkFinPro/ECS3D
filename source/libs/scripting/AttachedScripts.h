#ifndef ATTACHEDSCRIPTS_H
#define ATTACHEDSCRIPTS_H

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <uuid.h>

// The set of (uuid, class) pairs that have a live managed instance, indexed by uuid so a hot path can
// ask "does this object have any attached script" without scanning. Pure bookkeeping, no CLR.
class AttachedScripts {
public:
  // Returns false (and changes nothing) when the pair is already attached.
  bool attach(const uuids::uuid& uuid, const std::string& className);

  // Returns false (and changes nothing) when the pair is not attached.
  bool detach(const uuids::uuid& uuid, const std::string& className);

  void clear();

  [[nodiscard]] bool isAttached(const uuids::uuid& uuid, const std::string& className) const;

  [[nodiscard]] bool hasAnyAttached(const uuids::uuid& uuid) const;

  [[nodiscard]] bool empty() const;

private:
  std::unordered_map<uuids::uuid, std::unordered_set<std::string>> m_classesByUUID;
};

#endif //ATTACHEDSCRIPTS_H
