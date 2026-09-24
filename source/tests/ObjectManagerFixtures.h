#ifndef OBJECTMANAGERFIXTURES_H
#define OBJECTMANAGERFIXTURES_H

#include "TestScene.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <memory>
#include <uuid.h>
#include <vector>

namespace objectManagerFixtures {
  using fixtures::addChildObject;
  using fixtures::addObject;
  using fixtures::makeScene;
  using fixtures::Scene;

  inline uuids::uuid unknownUUID()
  {
    return uuids::uuid::from_string("123e4567-e89b-12d3-a456-426614174000").value();
  }

  // Every uuid in a subtree, root included - used to check a duplicate collides with nothing in the
  // original it was copied from.
  inline void collectUUIDs(const std::shared_ptr<Object>& object, std::vector<uuids::uuid>& out)
  {
    out.push_back(object->getUUID());

    for (const auto& child : object->getChildren())
    {
      collectUUIDs(child, out);
    }
  }

  // True when no uuid in one set also appears in the other - used to prove two subtrees share no uuids.
  inline bool disjointUUIDs(const std::vector<uuids::uuid>& a, const std::vector<uuids::uuid>& b)
  {
    return std::ranges::none_of(a, [&b](const auto& uuid) { return std::ranges::find(b, uuid) != b.end(); });
  }

  // True when every uuid in the set is distinct from every other - used to rule out a subtree that
  // reuses the same uuid across its own nodes.
  inline bool noDuplicateUUIDs(const std::vector<uuids::uuid>& values)
  {
    for (std::size_t i = 0; i < values.size(); ++i)
    {
      for (std::size_t j = i + 1; j < values.size(); ++j)
      {
        if (values[i] == values[j])
        {
          return false;
        }
      }
    }

    return true;
  }

  // Loads a manager's own serialize() output the way SceneAsset::loadObjects does: each root object is
  // reconstructed with its authored uuid, then its children are attached the same way loadChildren does.
  inline void loadObjects(const Scene& scene, const nlohmann::json& objectsData)
  {
    for (const auto& objectData : objectsData)
    {
      auto object = std::make_shared<Object>(objectData, scene.objectManager.get());
      scene.objectManager->addObject(object);

      if (objectData.contains("children"))
      {
        object->loadChildren(objectData.at("children"));
      }
    }
  }
}

#endif // OBJECTMANAGERFIXTURES_H
