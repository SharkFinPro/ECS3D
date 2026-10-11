#include "ScriptSystemTestFixtures.h"

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
}
