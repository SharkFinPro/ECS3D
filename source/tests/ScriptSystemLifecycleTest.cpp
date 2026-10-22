#include "ScriptSystemTestFixtures.h"

#include "ComponentRegistry.h"
#include "scenes/SceneAsset.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {
  using namespace scriptSystemFixtures;

  TEST_F(ScriptSystemTest, StartAttachesAndStartsEachScriptOnce)
  {
    const auto a = addScriptedObject("A", { "Mover", "Spinner" });
    const auto b = addScriptedObject("B", { "Mover" });

    scriptSystem.start(manager());

    for (const auto& [object, className] : { std::pair{ a, "Mover" }, std::pair{ a, "Spinner" },
                                             std::pair{ b, "Mover" } })
    {
      EXPECT_EQ(count(call("attach", object, className)), 1) << id(object) << " " << className;
      EXPECT_EQ(count(call("start", object, className)), 1) << id(object) << " " << className;
      EXPECT_LT(indexOf(call("attach", object, className)), indexOf(call("start", object, className)));
    }

    scriptSystem.start(manager());

    EXPECT_EQ(count(call("attach", a, "Mover")), 1);
    EXPECT_EQ(count(call("start", a, "Mover")), 1);
    EXPECT_EQ(count(call("start", b, "Mover")), 1);
  }

  TEST_F(ScriptSystemTest, TheRuntimeIsBuiltLazilyAndRetriedAfterAFailedBuild)
  {
    addScriptedObject("A", { "Mover" });
    int attempts = 0;
    ScriptSystem flaky([&attempts, this]() -> std::unique_ptr<ScriptRuntime>
    {
      if (++attempts == 1)
      {
        throw std::runtime_error("bridge missing");
      }

      return std::make_unique<RecordingRuntime>(state);
    });

    EXPECT_EQ(attempts, 0);
    EXPECT_THROW(flaky.start(manager()), std::runtime_error);
    EXPECT_EQ(attempts, 1);
    EXPECT_TRUE(state.calls.empty());

    flaky.start(manager());

    EXPECT_EQ(attempts, 2);
    EXPECT_FALSE(state.calls.empty());
  }

  TEST_F(ScriptSystemTest, WarmUpBuildsTheRuntimeOnceAndRetriesAfterAFailedBuild)
  {
    addScriptedObject("A", { "Mover" });
    int attempts = 0;
    ScriptSystem flaky([&attempts, this]() -> std::unique_ptr<ScriptRuntime>
    {
      if (++attempts == 1)
      {
        throw std::runtime_error("bridge missing");
      }

      return std::make_unique<RecordingRuntime>(state);
    });

    EXPECT_THROW(flaky.warmUp(), std::runtime_error);
    EXPECT_EQ(attempts, 1);

    flaky.warmUp();
    EXPECT_EQ(attempts, 2);

    flaky.warmUp();
    flaky.start(manager());
    EXPECT_EQ(attempts, 2);
    EXPECT_FALSE(state.calls.empty());
  }

  TEST_F(ScriptSystemTest, FixedUpdateAttachesAndStartsAScriptAddedToARunningScene)
  {
    const auto a = addScriptedObject("A", { "Mover" });
    const auto b = addScriptedObject("B", {});
    scriptSystem.start(manager());
    ASSERT_EQ(count(call("attach", b, "Spinner")), 0);

    addScript(b, "Spinner");
    scriptSystem.fixedUpdate(manager(), 0.02f);
    scriptSystem.fixedUpdate(manager(), 0.02f);

    EXPECT_EQ(count(call("attach", b, "Spinner")), 1);
    EXPECT_EQ(count(call("start", b, "Spinner")), 1);
    EXPECT_EQ(count(call("fixed", b, "Spinner")), 2);
    EXPECT_LT(indexOf(call("start", b, "Spinner")), indexOf(call("fixed", b, "Spinner")));
    EXPECT_EQ(count(call("attach", a, "Mover")), 1);
    EXPECT_EQ(count(call("start", a, "Mover")), 1);
    EXPECT_EQ(count(call("fixed", a, "Mover")), 2);
  }

  TEST_F(ScriptSystemTest, VariableUpdateSkipsAScriptThatIsNotAttachedYet)
  {
    const auto a = addScriptedObject("A", { "Mover" });
    scriptSystem.start(manager());
    addScript(a, "Spinner");

    scriptSystem.variableUpdate(manager());

    EXPECT_EQ(count(call("variable", a, "Mover")), 1);
    EXPECT_EQ(count(call("variable", a, "Spinner")), 0);

    scriptSystem.fixedUpdate(manager(), 0.02f);
    scriptSystem.variableUpdate(manager());

    EXPECT_EQ(count(call("variable", a, "Spinner")), 1);
  }

  TEST_F(ScriptSystemTest, AScriptAttachedBeforeTheSceneRunsIsStartedByTheFirstUpdate)
  {
    const auto a = addScriptedObject("A", { "Mover" });
    scriptSystem.attachAll(manager());
    ASSERT_EQ(count(call("attach", a, "Mover")), 1);
    ASSERT_EQ(count(call("start", a, "Mover")), 0);

    scriptSystem.fixedUpdate(manager(), 0.02f);
    scriptSystem.fixedUpdate(manager(), 0.02f);

    EXPECT_EQ(count(call("attach", a, "Mover")), 1);
    EXPECT_EQ(count(call("start", a, "Mover")), 1);
  }

  TEST_F(ScriptSystemTest, RemovingAScriptComponentStopsAndDetachesItOnTheNextTick)
  {
    const auto a = addScriptedObject("A", { "Mover" });
    const auto script = addScript(a, "Spinner");
    scriptSystem.start(manager());

    a->removeComponent(script);
    scriptSystem.fixedUpdate(manager(), 0.02f);

    EXPECT_EQ(count(call("stop", a, "Spinner")), 1);
    EXPECT_EQ(count(call("detach", a, "Spinner")), 1);
    EXPECT_LT(indexOf(call("stop", a, "Spinner")), indexOf(call("detach", a, "Spinner")));
    EXPECT_EQ(count(call("fixed", a, "Spinner")), 0);
    EXPECT_EQ(count(call("fixed", a, "Mover")), 1);
    EXPECT_EQ(count(call("detach", a, "Mover")), 0);

    scriptSystem.fixedUpdate(manager(), 0.02f);

    EXPECT_EQ(count(call("detach", a, "Spinner")), 1);
  }

  TEST_F(ScriptSystemTest, DestroyingAnObjectStopsAndDetachesItsScripts)
  {
    const auto a = addScriptedObject("A", { "Mover" });
    const auto b = addScriptedObject("B", { "Mover" });
    scriptSystem.start(manager());

    manager().removeObject(a);
    manager().deleteObjectsMarkedForDeletion();
    scriptSystem.fixedUpdate(manager(), 0.02f);

    EXPECT_EQ(count(call("stop", a, "Mover")), 1);
    EXPECT_EQ(count(call("detach", a, "Mover")), 1);
    EXPECT_EQ(count(call("detach", b, "Mover")), 0);
    EXPECT_EQ(count(call("fixed", b, "Mover")), 1);
  }

  TEST_F(ScriptSystemTest, AnInstanceThatWasNeverStartedIsDetachedWithoutBeingStopped)
  {
    const auto a = addScriptedObject("A", {});
    const auto script = addScript(a, "Mover");
    scriptSystem.attachAll(manager());
    ASSERT_EQ(count(call("attach", a, "Mover")), 1);

    a->removeComponent(script);
    scriptSystem.attachAll(manager());

    EXPECT_EQ(count(call("detach", a, "Mover")), 1);
    EXPECT_EQ(count(call("stop", a, "Mover")), 0);
  }

  TEST_F(ScriptSystemTest, AReplacementScriptOfTheSameClassGetsAFreshInstance)
  {
    const auto a = addScriptedObject("A", {});
    const auto oldScript = addScript(a, "Mover");
    scriptSystem.start(manager());
    ASSERT_EQ(count(call("attach", a, "Mover")), 1);

    a->removeComponent(oldScript);
    addScript(a, "Mover");
    scriptSystem.fixedUpdate(manager(), 0.02f);

    EXPECT_EQ(count(call("stop", a, "Mover")), 1);
    EXPECT_EQ(count(call("detach", a, "Mover")), 1);
    EXPECT_EQ(count(call("attach", a, "Mover")), 2);
    EXPECT_EQ(count(call("start", a, "Mover")), 2);
    EXPECT_EQ(count(call("fixed", a, "Mover")), 1);

    scriptSystem.fixedUpdate(manager(), 0.02f);

    EXPECT_EQ(count(call("detach", a, "Mover")), 1);
    EXPECT_EQ(count(call("attach", a, "Mover")), 2);
    EXPECT_EQ(count(call("fixed", a, "Mover")), 2);
  }

  TEST_F(ScriptSystemTest, AReplacementIsStillDetectedWhenTheOldScriptComponentIsDestroyed)
  {
    const auto a = addScriptedObject("A", {});
    auto oldScript = addScript(a, "Mover");
    scriptSystem.start(manager());
    ASSERT_EQ(count(call("attach", a, "Mover")), 1);

    a->removeComponent(oldScript);
    oldScript.reset();
    addScript(a, "Mover");
    scriptSystem.fixedUpdate(manager(), 0.02f);

    EXPECT_EQ(count(call("stop", a, "Mover")), 1);
    EXPECT_EQ(count(call("detach", a, "Mover")), 1);
    EXPECT_EQ(count(call("attach", a, "Mover")), 2);
    EXPECT_EQ(count(call("start", a, "Mover")), 2);
  }

  TEST_F(ScriptSystemTest, StopStopsAndDetachesEveryInstanceAndLeavesNothingAttached)
  {
    const auto a = addScriptedObject("A", { "Mover", "Spinner" });
    const auto b = addScriptedObject("B", { "Mover" });
    scriptSystem.start(manager());
    scriptSystem.fixedUpdate(manager(), 0.02f);

    scriptSystem.stop(manager());

    for (const auto& [object, className] : { std::pair{ a, "Mover" }, std::pair{ a, "Spinner" },
                                             std::pair{ b, "Mover" } })
    {
      EXPECT_EQ(count(call("stop", object, className)), 1) << id(object) << " " << className;
      EXPECT_EQ(count(call("detach", object, className)), 1) << id(object) << " " << className;
      EXPECT_LT(indexOf(call("stop", object, className)), indexOf(call("detach", object, className)));
    }

    state.calls.clear();
    scriptSystem.dispatchCollisionEvent(manager(), a->getUUID(), b->getUUID(), CollisionEvent::enter);
    scriptSystem.variableUpdate(manager());
    scriptSystem.stop(manager());

    EXPECT_TRUE(state.calls.empty());
  }

  TEST_F(ScriptSystemTest, StopAlsoStopsAnInstanceWhoseScriptLeftDuringTheRun)
  {
    const auto a = addScriptedObject("A", { "Mover" });
    const auto script = addScript(a, "Spinner");
    scriptSystem.start(manager());
    a->removeComponent(script);

    scriptSystem.stop(manager());

    EXPECT_EQ(count(call("stop", a, "Spinner")), 1);
    EXPECT_EQ(count(call("detach", a, "Spinner")), 1);
    EXPECT_EQ(count(call("stop", a, "Mover")), 1);
  }

  TEST_F(ScriptSystemTest, StopBeforeAnythingStartedDoesNothing)
  {
    const auto a = addScriptedObject("A", { "Mover" });

    scriptSystem.stop(manager());

    EXPECT_EQ(state.created, 0);
    EXPECT_TRUE(state.calls.empty());

    scriptSystem.start(manager());

    EXPECT_EQ(state.created, 1);
    EXPECT_EQ(count(call("attach", a, "Mover")), 1);
  }

  const auto spawnSceneUUID = uuids::uuid::from_string("99999999-9999-9999-9999-999999999999").value();
  constexpr std::size_t spawnsPerCallback = 64;

  class ScriptSpawnDuringLifecycleTest : public ScriptSystemTest {
  protected:
    std::shared_ptr<SceneAsset> spawnScene =
      std::make_shared<SceneAsset>(spawnSceneUUID, "Spawn", scene.componentRegistry);
    std::vector<std::shared_ptr<Object>> scripted;
    std::size_t sizeBefore = 0;
    std::size_t callbacks = 0;
    bool listGrewInside = false;

    [[nodiscard]] ObjectManager& sceneObjects() const
    {
      return *spawnScene->getObjectManager();
    }

    void addScripted(const std::string& name, const std::string& className)
    {
      const auto object = std::make_shared<Object>(name);
      sceneObjects().addObject(object);
      addScript(object, className);
      scripted.push_back(object);
    }

    void addScriptedObjects()
    {
      addScripted("A", "Mover");
      addScripted("B", "Mover");
      addScripted("C", "Mover");
      sizeBefore = sceneObjects().getAllObjects().size();
    }

    [[nodiscard]] std::function<void(const std::string&, const std::string&)> spawner()
    {
      return [this](const std::string&, const std::string&)
      {
        ++callbacks;
        for (std::size_t i = 0; i < spawnsPerCallback; ++i)
        {
          sceneObjects().addObject(std::make_shared<Object>("Spawned"));
        }

        if (sceneObjects().getAllObjects().size() != sizeBefore)
        {
          listGrewInside = true;
        }
      };
    }

    void expectDeferred(const std::size_t expectedCallbacks)
    {
      EXPECT_EQ(callbacks, expectedCallbacks);
      EXPECT_FALSE(listGrewInside);
      EXPECT_EQ(sceneObjects().getAllObjects().size(), sizeBefore);
      EXPECT_EQ(sceneObjects().getPendingAdditions().size(), expectedCallbacks * spawnsPerCallback);
    }
  };

  TEST_F(ScriptSpawnDuringLifecycleTest, ObjectScriptStartThatSpawnsDefersTheSpawnsOnTheSceneOverload)
  {
    addScriptedObjects();
    state.startHook = spawner();

    spawnScene->start();
    scriptSystem.start(*spawnScene);

    expectDeferred(scripted.size());
    for (const auto& object : scripted)
    {
      EXPECT_EQ(count(call("start", object, "Mover")), 1) << id(object);
    }

    sceneObjects().flushPendingAdditions();

    EXPECT_EQ(sceneObjects().getAllObjects().size(), sizeBefore + scripted.size() * spawnsPerCallback);
    EXPECT_TRUE(sceneObjects().getPendingAdditions().empty());
  }

  TEST_F(ScriptSpawnDuringLifecycleTest, ObjectScriptStartThatSpawnsDefersTheSpawnsOnTheManagerOverload)
  {
    addScriptedObjects();
    state.startHook = spawner();

    scriptSystem.start(sceneObjects());

    expectDeferred(scripted.size());
    for (const auto& object : scripted)
    {
      EXPECT_EQ(count(call("start", object, "Mover")), 1) << id(object);
    }
  }

  TEST_F(ScriptSpawnDuringLifecycleTest, SceneScriptStartThatSpawnsDefersTheSpawns)
  {
    addScriptedObjects();
    const auto sceneScript = std::make_shared<Script>("Director");
    ASSERT_TRUE(spawnScene->addScript(sceneScript));
    state.startHook = [this, inner = spawner()](const std::string& uuid, const std::string& className)
    {
      if (className == "Director")
      {
        inner(uuid, className);
      }
    };

    spawnScene->start();
    scriptSystem.start(*spawnScene);

    expectDeferred(1);
    EXPECT_EQ(count(describe("start", uuids::to_string(spawnSceneUUID).c_str(), "Director")), 1);
    for (const auto& object : scripted)
    {
      EXPECT_EQ(count(call("start", object, "Mover")), 1) << id(object);
    }
  }

  TEST_F(ScriptSpawnDuringLifecycleTest, ObjectScriptStopThatSpawnsDefersTheSpawnsAndTheRebuildDropsThem)
  {
    addScriptedObjects();

    spawnScene->start();
    scriptSystem.start(*spawnScene);
    ASSERT_TRUE(sceneObjects().getPendingAdditions().empty());

    state.stopHook = spawner();
    scriptSystem.stop(*spawnScene);

    expectDeferred(scripted.size());
    for (const auto& object : scripted)
    {
      EXPECT_EQ(count(call("stop", object, "Mover")), 1) << id(object);
    }

    spawnScene->stop();

    EXPECT_TRUE(sceneObjects().getPendingAdditions().empty());
    sceneObjects().flushPendingAdditions();
    EXPECT_EQ(sceneObjects().getAllObjects().size(), sizeBefore);
    for (const auto& object : scripted)
    {
      EXPECT_NE(sceneObjects().getObjectByUUID(object->getUUID()), nullptr) << id(object);
    }
  }
}
