#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/collisions/BoxCollider.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"

#include <glm/vec3.hpp>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
  const auto firstUUID = uuids::uuid::from_string("11111111-1111-1111-1111-111111111111").value();
  const auto secondUUID = uuids::uuid::from_string("22222222-2222-2222-2222-222222222222").value();
  const auto missingUUID = uuids::uuid::from_string("99999999-9999-9999-9999-999999999999").value();

  std::shared_ptr<ComponentRegistry> makeRegistry()
  {
    auto componentRegistry = std::make_shared<ComponentRegistry>();
    registerDataComponents(*componentRegistry);

    return componentRegistry;
  }

  // A scene holding one object with a collider, so start/stop are observable through the collider's
  // authored-versus-live value.
  std::shared_ptr<SceneAsset> makeScene(const uuids::uuid& uuid, const std::string& name,
                                        const std::shared_ptr<ComponentRegistry>& componentRegistry,
                                        std::shared_ptr<BoxCollider>* outCollider = nullptr)
  {
    auto scene = std::make_shared<SceneAsset>(uuid, name, componentRegistry);

    const auto object = std::make_shared<Object>("Object");
    scene->getObjectManager()->addObject(object);

    const auto collider = std::make_shared<BoxCollider>();
    object->addComponent(collider);

    if (outCollider)
    {
      *outCollider = collider;
    }

    return scene;
  }
}

TEST(SceneManager, StartsWithNoSceneAndNothingRunning)
{
  const SceneManager sceneManager;

  EXPECT_EQ(sceneManager.getCurrentScene(), nullptr);
  EXPECT_EQ(sceneManager.getSceneStatus(), SceneStatus::stopped);
  EXPECT_TRUE(sceneManager.getScenes().empty());
}

TEST(SceneManager, FindsAnAddedSceneByUuid)
{
  const auto componentRegistry = makeRegistry();

  SceneManager sceneManager;
  const auto scene = makeScene(firstUUID, "Main", componentRegistry);
  sceneManager.addScene(scene);

  EXPECT_EQ(sceneManager.getScene(firstUUID), scene);
  EXPECT_EQ(sceneManager.getScene(missingUUID), nullptr);
  EXPECT_EQ(sceneManager.getScenes().size(), 1u);
}

TEST(SceneManager, AddingTheSameUuidTwiceKeepsTheFirst)
{
  const auto componentRegistry = makeRegistry();

  SceneManager sceneManager;
  const auto scene = makeScene(firstUUID, "Main", componentRegistry);
  sceneManager.addScene(scene);
  sceneManager.addScene(makeScene(firstUUID, "Impostor", componentRegistry));

  ASSERT_EQ(sceneManager.getScenes().size(), 1u);
  EXPECT_EQ(sceneManager.getScene(firstUUID), scene);
}

TEST(SceneManager, RefusesToLoadASceneThatIsNotThere)
{
  SceneManager sceneManager;

  EXPECT_THROW(sceneManager.loadScene(nullptr), std::runtime_error);
  EXPECT_EQ(sceneManager.getCurrentScene(), nullptr);
}

TEST(SceneManager, StartingRunsTheSceneAndStoppingRestoresIt)
{
  const auto componentRegistry = makeRegistry();

  std::shared_ptr<BoxCollider> collider;
  SceneManager sceneManager;
  const auto scene = makeScene(firstUUID, "Main", componentRegistry, &collider);
  sceneManager.addScene(scene);
  sceneManager.loadScene(scene);

  sceneManager.startScene();
  EXPECT_EQ(sceneManager.getSceneStatus(), SceneStatus::running);

  collider->setScale(glm::vec3(4));
  EXPECT_EQ(collider->getLocalScale(), glm::vec3(4));

  sceneManager.resetScene();

  // Stop is what makes a run non-destructive: the scene goes back to what it would save.
  EXPECT_EQ(sceneManager.getSceneStatus(), SceneStatus::stopped);
  EXPECT_EQ(collider->getLocalScale(), glm::vec3(1));
}

TEST(SceneManager, ResumingFromPauseDoesNotRestartTheRun)
{
  const auto componentRegistry = makeRegistry();

  std::shared_ptr<BoxCollider> collider;
  SceneManager sceneManager;
  const auto scene = makeScene(firstUUID, "Main", componentRegistry, &collider);
  sceneManager.addScene(scene);
  sceneManager.loadScene(scene);

  sceneManager.startScene();
  collider->setScale(glm::vec3(4));

  sceneManager.pauseScene();
  EXPECT_EQ(sceneManager.getSceneStatus(), SceneStatus::paused);

  sceneManager.startScene();

  // A second start only re-seeds live values on a real stopped-to-running transition; resuming must
  // leave the run where it was.
  EXPECT_EQ(sceneManager.getSceneStatus(), SceneStatus::running);
  EXPECT_EQ(collider->getLocalScale(), glm::vec3(4));
}

TEST(SceneManager, SwitchingScenesStopsTheOutgoingOne)
{
  const auto componentRegistry = makeRegistry();

  std::shared_ptr<BoxCollider> collider;
  SceneManager sceneManager;
  const auto first = makeScene(firstUUID, "First", componentRegistry, &collider);
  const auto second = makeScene(secondUUID, "Second", componentRegistry);
  sceneManager.addScene(first);
  sceneManager.addScene(second);

  sceneManager.loadScene(first);
  sceneManager.startScene();
  collider->setScale(glm::vec3(4));

  sceneManager.loadScene(second);

  EXPECT_EQ(sceneManager.getCurrentScene(), second);
  EXPECT_EQ(sceneManager.getSceneStatus(), SceneStatus::stopped);

  // The scene being left has to be stopped, or it keeps its run state and would resume mid-flight.
  EXPECT_EQ(collider->getLocalScale(), glm::vec3(1));
}

TEST(SceneManager, DoesNothingWithNoSceneLoaded)
{
  const auto componentRegistry = makeRegistry();

  SceneManager sceneManager;
  sceneManager.addScene(makeScene(firstUUID, "Main", componentRegistry));

  // Added but never loaded, so there is nothing to run.
  sceneManager.startScene();
  EXPECT_EQ(sceneManager.getSceneStatus(), SceneStatus::stopped);

  sceneManager.pauseScene();
  EXPECT_EQ(sceneManager.getSceneStatus(), SceneStatus::stopped);

  sceneManager.resetScene();
  EXPECT_EQ(sceneManager.getSceneStatus(), SceneStatus::stopped);
}

TEST(SceneManager, ClearDropsEverythingAndTheRunState)
{
  const auto componentRegistry = makeRegistry();

  SceneManager sceneManager;
  const auto scene = makeScene(firstUUID, "Main", componentRegistry);
  sceneManager.addScene(scene);
  sceneManager.loadScene(scene);
  sceneManager.startScene();

  sceneManager.clear();

  EXPECT_TRUE(sceneManager.getScenes().empty());
  EXPECT_EQ(sceneManager.getCurrentScene(), nullptr);
  EXPECT_EQ(sceneManager.getSceneStatus(), SceneStatus::stopped);
}
