#ifndef EDITHISTORYFIXTURES_H
#define EDITHISTORYFIXTURES_H

#include <gtest/gtest.h>

#include "TestScene.h"
#include "Replication.h"
#include "edits/EditCommand.h"
#include "edits/EditHistory.h"
#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Script.h"
#include "objects/components/Transform.h"

#include <memory>
#include <string>
#include <uuid.h>

namespace editHistoryFixtures {
  using fixtures::expectNear;
  using fixtures::transformOf;

  // Every object-kind test edits a scene that already holds one object, so the fixture carries one.
  struct Scene : fixtures::Scene {
    std::shared_ptr<Object> object;
  };

  inline Scene makeScene()
  {
    Scene scene;
    scene.object = addObject(scene, "Object");
    return scene;
  }

  // Asset-kind tests need a registry but not necessarily any objects; still carries an ObjectManager
  // because EditHistory::undo/redo take one unconditionally (unused by asset-kind commands).
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

  inline bool hasScript(const std::shared_ptr<Object>& object, const std::string& className)
  {
    for (const auto& script : object->getScripts())
    {
      if (const auto scriptComponent = std::dynamic_pointer_cast<Script>(script);
          scriptComponent && scriptComponent->getClassName() == className)
      {
        return true;
      }
    }

    return false;
  }

  // Two component edits to one object (x: 1 -> 2, then 2 -> 4), recorded and then both undone, so both
  // sit on the redo stack with the transform back at its first before state.
  inline void recordTwoEditsAndUndoBoth(Scene& scene, edits::EditHistory& history)
  {
    const auto transform = transformOf(scene.object);

    transform->setPosition({ 1, 0, 0 });
    const auto before1 = transform->serialize();
    transform->setPosition({ 2, 0, 0 });
    const auto after1 = transform->serialize();

    history.record(edits::EditCommand::componentEdit(scene.object->getUUID(), before1, after1));

    const auto before2 = transform->serialize();
    transform->setPosition({ 4, 0, 0 });
    const auto after2 = transform->serialize();
    history.record(edits::EditCommand::componentEdit(scene.object->getUUID(), before2, after2));

    auto outcome = history.undo(*scene.objectManager);
    ASSERT_TRUE(outcome.ok());
    replication::applyComponentEdit(*scene.objectManager, *outcome.messagePayload);
    outcome = history.undo(*scene.objectManager);
    ASSERT_TRUE(outcome.ok());
    replication::applyComponentEdit(*scene.objectManager, *outcome.messagePayload);
  }
}

#endif // EDITHISTORYFIXTURES_H
