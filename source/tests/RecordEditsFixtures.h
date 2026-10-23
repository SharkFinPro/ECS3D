#ifndef RECORDEDITSFIXTURES_H
#define RECORDEDITSFIXTURES_H

#include "TestScene.h"
#include "assets/AssetRegistry.h"
#include "objects/Object.h"

#include <memory>
#include <string>
#include <uuid.h>

namespace recordEditsFixtures {
  // Every scene-edit test derives a command from a view that already holds one root object.
  struct Scene : fixtures::Scene {
    std::shared_ptr<Object> object;
  };

  inline Scene makeScene()
  {
    Scene scene;
    scene.object = addObject(scene, "Object");
    return scene;
  }

  // Asset-kind tests need a registry; the ObjectManager comes along because EditHistory::undo/redo take
  // one unconditionally (unused by asset-kind commands).
  struct AssetScene : fixtures::Scene {
    AssetRegistry assetRegistry;
  };

  inline uuids::uuid someOtherUUID()
  {
    return uuids::uuid::from_string("123e4567-e89b-12d3-a456-426614174000").value();
  }

  inline uuids::uuid anotherUUID()
  {
    return uuids::uuid::from_string("00000000-0000-0000-0000-000000000001").value();
  }

  inline std::string uuidString(const uuids::uuid& uuid)
  {
    return uuids::to_string(uuid);
  }

  inline const std::string oldPrefabBody = R"({"name":"Old"})";
  inline const std::string newPrefabBody = R"({"name":"New"})";
}

#endif // RECORDEDITSFIXTURES_H
