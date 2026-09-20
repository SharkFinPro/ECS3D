#ifndef SCENEEDITFIXTURES_H
#define SCENEEDITFIXTURES_H

#include "TestScene.h"
#include "Replication.h"
#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Transform.h"

#include <cstddef>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <uuid.h>
#include <vector>

namespace sceneEditFixtures {
  using replication::SceneEditResult;
  using fixtures::expectNear;
  using fixtures::transformOf;

  // Every test here edits a scene that already holds an object, so the fixture carries one.
  struct Scene : fixtures::Scene {
    std::shared_ptr<Object> object;
  };

  inline Scene makeScene()
  {
    Scene scene;
    scene.object = addObject(scene, "Object");

    return scene;
  }

  // Not named "apply": an unqualified call with a nlohmann::json argument finds std::apply by argument
  // lookup, because basic_json is parameterized on std::map/std::vector/std::string and that makes std an
  // associated namespace. It compiles on some standard libraries and not others.
  inline SceneEditResult applyEdit(const Scene& scene, const nlohmann::json& edit,
                            const AssetRegistry* assetRegistry = nullptr)
  {
    return replication::applySceneEdit(*scene.objectManager, edit, assetRegistry);
  }

  inline uuids::uuid someOtherUUID()
  {
    return uuids::uuid::from_string("123e4567-e89b-12d3-a456-426614174000").value();
  }

  // A second fixed uuid, distinct from someOtherUUID(), for tests that need two uuids that name different
  // things (e.g. a prefab and a parent that is not in the scene).
  inline uuids::uuid anotherUUID()
  {
    return uuids::uuid::from_string("00000000-0000-0000-0000-000000000001").value();
  }

  // A trivial but well-formed prefab body: no components/scripts/children, just enough for instantiate to
  // succeed. The uuid field is overwritten by reassignUUIDs on instantiation, so its value doesn't matter.
  inline nlohmann::json trivialBody()
  {
    return {
      { "name", "Block" },
      { "uuid", uuids::to_string(someOtherUUID()) },
      { "components", nlohmann::json::array() },
      { "scripts", nlohmann::json::array() },
      { "children", nlohmann::json::array() }
    };
  }

  // A prefab body two levels deep: root, one child, one grandchild. Enough to straddle the depth limit
  // without a whole chainOfDepth loop - this suite only ever needs the two nested levels.
  inline nlohmann::json twoLevelBody()
  {
    nlohmann::json grandchild = trivialBody();
    grandchild["name"] = "Grandchild";

    const nlohmann::json child = {
      { "name", "Child" },
      { "uuid", uuids::to_string(someOtherUUID()) },
      { "components", nlohmann::json::array() },
      { "scripts", nlohmann::json::array() },
      { "children", nlohmann::json::array({ grandchild }) }
    };

    return {
      { "name", "Root" },
      { "uuid", uuids::to_string(someOtherUUID()) },
      { "components", nlohmann::json::array() },
      { "scripts", nlohmann::json::array() },
      { "children", nlohmann::json::array({ child }) }
    };
  }

  // The node `levels` steps below start, chained through addChildObject - the same shape ObjectSpawnTest
  // uses for its own depth fixtures, kept local here since each suite builds its chain through this
  // suite's own Scene type.
  inline std::shared_ptr<Object> descendantAtDepth(const Scene& scene, const std::shared_ptr<Object>& start,
                                            const std::size_t levels)
  {
    auto current = start;
    for (std::size_t i = 0; i < levels; ++i)
    {
      current = addChildObject(scene, "Descendant", current);
    }

    return current;
  }

  // The names of a sibling list in order - what every reorderObject assertion below checks, since a
  // uuid-by-uuid comparison would not show a mis-ordering as clearly as a mismatched name sequence does.
  inline std::vector<std::string> namesOf(const std::vector<std::shared_ptr<Object>>& objects)
  {
    std::vector<std::string> names;
    names.reserve(objects.size());
    for (const auto& object : objects)
    {
      names.push_back(object->getName());
    }

    return names;
  }
}

#endif // SCENEEDITFIXTURES_H
