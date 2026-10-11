#ifndef SCENEOBJECTDIFF_H
#define SCENEOBJECTDIFF_H

#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include <algorithm>
#include <iterator>
#include <memory>
#include <unordered_set>
#include <vector>
#include <uuid.h>

namespace sceneObjectDiff {
  struct Diff {
    // In the order the after-list holds them.
    std::vector<uuids::uuid> added;
    // In the order the before-list held them.
    std::vector<uuids::uuid> removed;
  };

  // The object, then each child's subtree depth-first in child order.
  inline void appendSubtree(const Object& root, std::vector<uuids::uuid>& out)
  {
    out.push_back(root.getUUID());
    for (const auto& child : root.getChildren())
    {
      appendSubtree(*child, out);
    }
  }

  [[nodiscard]] inline std::vector<uuids::uuid> flattenSubtrees(const std::vector<std::shared_ptr<Object>>& roots)
  {
    std::vector<uuids::uuid> out;
    for (const auto& root : roots)
    {
      appendSubtree(*root, out);
    }

    return out;
  }

  // Every object in the scene, parents before their children, depth-first from the roots in order.
  [[nodiscard]] inline std::vector<uuids::uuid> collectUUIDs(const ObjectManager& objectManager)
  {
    return flattenSubtrees(objectManager.getObjects());
  }

  [[nodiscard]] inline Diff diff(const std::vector<uuids::uuid>& before, const std::vector<uuids::uuid>& after)
  {
    const std::unordered_set<uuids::uuid> beforeSet(before.begin(), before.end());
    const std::unordered_set<uuids::uuid> afterSet(after.begin(), after.end());

    Diff result;
    std::ranges::copy_if(after, std::back_inserter(result.added),
                         [&beforeSet](const uuids::uuid& id) { return !beforeSet.contains(id); });
    std::ranges::copy_if(before, std::back_inserter(result.removed),
                         [&afterSet](const uuids::uuid& id) { return !afterSet.contains(id); });

    return result;
  }
}

#endif //SCENEOBJECTDIFF_H
