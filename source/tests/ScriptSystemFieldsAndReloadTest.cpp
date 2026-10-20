#include "ScriptSystemTestFixtures.h"

#include "ComponentRegistry.h"
#include "scenes/SceneAsset.h"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace {
  using namespace scriptSystemFixtures;

  TEST_F(ScriptSystemTest, AttachWritesTheStoredFieldsOfEveryExposedType)
  {
    state.exposedByClass["Mover"] = exposedFieldsJson;
    const auto a = fixtures::addObject(scene, "A");
    addScript(a, "Mover", {
      { { "name", "speed" }, { "type", "float" }, { "value", 1.5 } },
      { { "name", "count" }, { "type", "int" }, { "value", 7 } },
      { { "name", "enabled" }, { "type", "bool" }, { "value", true } },
      { { "name", "offset" }, { "type", "vector3" }, { "value", nlohmann::json::array({ 1.0, 2.0, 3.0 }) } }
    });

    scriptSystem.start(manager());

    EXPECT_FLOAT_EQ(state.floats["speed"], 1.5f);
    EXPECT_EQ(state.ints["count"], 7);
    EXPECT_TRUE(state.bools["enabled"]);
    EXPECT_FLOAT_EQ(state.vectors["offset"].x, 1.0f);
    EXPECT_FLOAT_EQ(state.vectors["offset"].y, 2.0f);
    EXPECT_FLOAT_EQ(state.vectors["offset"].z, 3.0f);
    EXPECT_LT(indexOf(call("attach", a, "Mover")), indexOf("setFloat|speed"));
  }

  TEST_F(ScriptSystemTest, SyncFieldsToDataReadsEveryExposedTypeBack)
  {
    state.exposedByClass["Mover"] = exposedFieldsJson;
    const auto a = fixtures::addObject(scene, "A");
    const auto script = addScript(a, "Mover");
    scriptSystem.start(manager());
    state.floats["speed"] = 9.5f;
    state.ints["count"] = -4;
    state.bools["enabled"] = true;
    state.vectors["offset"] = glm::vec3(4.0f, 5.0f, 6.0f);

    scriptSystem.syncFieldsToData(manager());

    ASSERT_EQ(script->getFields().size(), 4u);
    EXPECT_EQ(findField(script, "speed").at("type"), "float");
    EXPECT_FLOAT_EQ(findField(script, "speed").at("value").get<float>(), 9.5f);
    EXPECT_EQ(findField(script, "count").at("type"), "int");
    EXPECT_EQ(findField(script, "count").at("value").get<int>(), -4);
    EXPECT_EQ(findField(script, "enabled").at("type"), "bool");
    EXPECT_TRUE(findField(script, "enabled").at("value").get<bool>());
    EXPECT_EQ(findField(script, "offset").at("type"), "vector3");
    EXPECT_EQ(findField(script, "offset").at("value").get<std::vector<float>>(),
              (std::vector<float>{ 4.0f, 5.0f, 6.0f }));
  }

  TEST_F(ScriptSystemTest, AttachWritesAStoredStringField)
  {
    state.exposedByClass["Mover"] = exposedStringFieldJson;
    const auto a = fixtures::addObject(scene, "A");
    addScript(a, "Mover", {
      { { "name", "label" }, { "type", "string" }, { "value", "Player One" } }
    });

    scriptSystem.start(manager());

    EXPECT_EQ(count("setString|label"), 1);
    EXPECT_EQ(state.strings["label"], "Player One");
  }

  TEST_F(ScriptSystemTest, AStoredNonStringValueIsNotWrittenToAStringField)
  {
    // Positive control is AttachWritesAStoredStringField above.
    state.exposedByClass["Mover"] = exposedStringFieldJson;
    const auto a = fixtures::addObject(scene, "A");
    addScript(a, "Mover", {
      { { "name", "label" }, { "type", "string" }, { "value", 5 } },
      { { "name", "count" }, { "type", "int" }, { "value", "5" } }
    });

    scriptSystem.start(manager());

    EXPECT_EQ(count("setString|label"), 0);
    EXPECT_EQ(count("setInt|count"), 0);
  }

  TEST_F(ScriptSystemTest, SyncFieldsToDataReadsAStringFieldBack)
  {
    state.exposedByClass["Mover"] = exposedStringFieldJson;
    const auto a = fixtures::addObject(scene, "A");
    const auto script = addScript(a, "Mover");
    scriptSystem.start(manager());
    state.strings["label"] = "Edited";

    scriptSystem.syncFieldsToData(manager());

    ASSERT_EQ(script->getFields().size(), 2u);
    EXPECT_EQ(findField(script, "label").at("type"), "string");
    EXPECT_EQ(findField(script, "label").at("value"), "Edited");
  }

  TEST_F(ScriptSystemTest, ApplyScriptFieldEditWritesAStringToAnAttachedInstance)
  {
    state.exposedByClass["Mover"] = exposedStringFieldJson;
    const auto attached = addScriptedObject("A", { "Mover" });
    scriptSystem.start(manager());
    const nlohmann::json edit = nlohmann::json::array({
      { { "name", "label" }, { "type", "string" }, { "value", "Renamed" } }
    });

    scriptSystem.applyScriptFieldEdit(attached->getUUID(), "Mover", edit);

    EXPECT_EQ(state.strings["label"], "Renamed");
  }

  TEST_F(ScriptSystemTest, AStoredFieldWhoseTypeDisagreesWithTheInstanceIsNotWritten)
  {
    state.exposedByClass["Mover"] = exposedFieldsJson;
    const auto a = fixtures::addObject(scene, "A");
    addScript(a, "Mover", {
      { { "name", "count" }, { "type", "float" }, { "value", 2.5 } },
      { { "name", "ghost" }, { "type", "int" }, { "value", 1 } },
      { { "name", "speed" }, { "type", "float" }, { "value", 3.0 } }
    });

    scriptSystem.start(manager());

    EXPECT_EQ(count("setFloat|speed"), 1);
    EXPECT_FLOAT_EQ(state.floats["speed"], 3.0f);
    EXPECT_EQ(count("setFloat|count"), 0);
    EXPECT_EQ(count("setInt|count"), 0);
    EXPECT_EQ(count("setInt|ghost"), 0);
  }

  TEST_F(ScriptSystemTest, ApplyScriptFieldEditWritesOnlyToAnAttachedInstance)
  {
    state.exposedByClass["Mover"] = exposedFieldsJson;
    const auto attached = addScriptedObject("A", { "Mover" });
    const auto detached = addScriptedObject("B", {});
    scriptSystem.start(manager());
    const nlohmann::json edit = nlohmann::json::array({
      { { "name", "count" }, { "type", "int" }, { "value", 12 } }
    });

    scriptSystem.applyScriptFieldEdit(detached->getUUID(), "Mover", edit);

    EXPECT_EQ(count("setInt|count"), 0);

    scriptSystem.applyScriptFieldEdit(attached->getUUID(), "Mover", edit);

    EXPECT_EQ(count("setInt|count"), 1);
    EXPECT_EQ(state.ints["count"], 12);
  }

  TEST_F(ScriptSystemTest, CollisionBetweenUnscriptedObjectsTouchesNeitherTheRuntimeNorTheScene)
  {
    const auto unknownA = uuids::uuid::from_string("00000000-0000-0000-0000-0000000000a1").value();
    const auto unknownB = uuids::uuid::from_string("00000000-0000-0000-0000-0000000000b1").value();

    scriptSystem.dispatchCollisionEvent(manager(), unknownA, unknownB, CollisionEvent::enter);

    EXPECT_EQ(state.created, 0);
    EXPECT_TRUE(state.calls.empty());

    const auto scripted = addScriptedObject("A", { "Mover" });
    const auto plain = addScriptedObject("B", {});
    scriptSystem.start(manager());
    state.calls.clear();

    scriptSystem.dispatchCollisionEvent(manager(), unknownA, unknownB, CollisionEvent::enter);

    EXPECT_TRUE(state.calls.empty());

    scriptSystem.dispatchCollisionEvent(manager(), plain->getUUID(), unknownA, CollisionEvent::enter);

    EXPECT_TRUE(state.calls.empty());

    scriptSystem.dispatchCollisionEvent(manager(), scripted->getUUID(), unknownB, CollisionEvent::enter);

    EXPECT_EQ(state.calls.size(), 1u);
    EXPECT_EQ(count(call("collision", scripted, "Mover") + "|" + uuids::to_string(unknownB) + "|0"), 1);
  }

  TEST_F(ScriptSystemTest, CollisionEventsReachEachScriptedSideWithThePhaseAndTheOtherObject)
  {
    const auto a = addScriptedObject("A", { "Mover", "Spinner" });
    const auto b = addScriptedObject("B", { "Mover" });
    scriptSystem.start(manager());
    state.calls.clear();

    scriptSystem.dispatchCollisionEvent(manager(), a->getUUID(), b->getUUID(), CollisionEvent::stay);

    EXPECT_EQ(state.calls.size(), 3u);
    EXPECT_EQ(count(call("collision", a, "Mover") + "|" + id(b) + "|1"), 1);
    EXPECT_EQ(count(call("collision", a, "Spinner") + "|" + id(b) + "|1"), 1);
    EXPECT_EQ(count(call("collision", b, "Mover") + "|" + id(a) + "|1"), 1);

    state.calls.clear();
    scriptSystem.dispatchCollisionEvent(manager(), a->getUUID(), b->getUUID(), CollisionEvent::exit);

    EXPECT_EQ(count(call("collision", a, "Mover") + "|" + id(b) + "|2"), 1);
    EXPECT_EQ(count(call("collision", b, "Mover") + "|" + id(a) + "|2"), 1);
  }

  TEST_F(ScriptSystemTest, ADestroyedObjectIsSkippedWhileTheSurvivorStillGetsTheEvent)
  {
    const auto a = addScriptedObject("A", { "Mover" });
    const auto b = addScriptedObject("B", { "Mover" });
    scriptSystem.start(manager());
    state.calls.clear();
    const auto bUUID = b->getUUID();
    const auto bId = id(b);
    manager().removeObject(b);
    manager().deleteObjectsMarkedForDeletion();

    scriptSystem.dispatchCollisionEvent(manager(), a->getUUID(), bUUID, CollisionEvent::exit);

    EXPECT_EQ(state.calls.size(), 1u);
    EXPECT_EQ(count(call("collision", a, "Mover") + "|" + bId + "|2"), 1);
  }
  class ScriptSystemHotReloadTest : public ScriptSystemTest {
  protected:
    std::filesystem::path scriptsDir;
    ScriptSystem reloading{ factory(), makeDir() };

    ~ScriptSystemHotReloadTest() override
    {
      std::error_code ec;
      std::filesystem::remove_all(scriptsDir, ec);
    }

    void writeScript(const std::string& name) const
    {
      std::ofstream file(scriptsDir / name);
      file << "class " << name << " {}";
    }

  private:
    std::filesystem::path makeDir()
    {
      const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
      scriptsDir = std::filesystem::temp_directory_path() / (std::string("ecs3d_scripts_") + info->name());
      std::error_code ec;
      std::filesystem::remove_all(scriptsDir, ec);
      std::filesystem::create_directories(scriptsDir);

      return scriptsDir;
    }
  };

  TEST_F(ScriptSystemHotReloadTest, ReloadsOnceWhenTheScriptDirectoryChangesAfterTheInterval)
  {
    writeScript("Existing.cs");
    const auto a = addScriptedObject("A", { "Mover" });
    reloading.start(manager());
    ASSERT_EQ(count(call("attach", a, "Mover")), 1);
    ASSERT_EQ(count("reload"), 0);

    writeScript("Added.cs");
    reloading.fixedUpdate(manager(), 0.1f);

    EXPECT_EQ(count("reload"), 0);
    EXPECT_EQ(count(call("attach", a, "Mover")), 1);

    reloading.fixedUpdate(manager(), 0.5f);

    EXPECT_EQ(count("reload"), 1);
    EXPECT_EQ(count(call("attach", a, "Mover")), 2);
    EXPECT_EQ(count(call("start", a, "Mover")), 2);

    reloading.fixedUpdate(manager(), 0.6f);
    reloading.fixedUpdate(manager(), 0.6f);

    EXPECT_EQ(count("reload"), 1);
    EXPECT_EQ(count(call("attach", a, "Mover")), 2);
  }

  TEST_F(ScriptSystemHotReloadTest, AnUnchangedDirectoryNeverReloads)
  {
    writeScript("Existing.cs");
    addScriptedObject("A", { "Mover" });
    reloading.start(manager());

    for (int i = 0; i < 5; ++i)
    {
      reloading.fixedUpdate(manager(), 0.6f);
    }

    EXPECT_EQ(count("reload"), 0);

    writeScript("Added.cs");
    reloading.fixedUpdate(manager(), 0.6f);

    EXPECT_EQ(count("reload"), 1);
  }

  TEST_F(ScriptSystemHotReloadTest, ACompileErrorKeepsTheAttachedInstancesAndWaitsForTheNextEdit)
  {
    const auto a = addScriptedObject("A", { "Mover" });
    reloading.start(manager());
    ASSERT_EQ(count(call("start", a, "Mover")), 1);
    state.reloadReplaces = false;
    writeScript("Broken.cs");

    reloading.fixedUpdate(manager(), 0.6f);

    EXPECT_EQ(count("reload"), 1);
    EXPECT_EQ(count(call("attach", a, "Mover")), 1);
    EXPECT_EQ(count(call("start", a, "Mover")), 1);
    EXPECT_EQ(count(call("detach", a, "Mover")), 0);
    EXPECT_EQ(count(call("fixed", a, "Mover")), 1);

    reloading.fixedUpdate(manager(), 0.6f);

    EXPECT_EQ(count("reload"), 1);

    state.reloadReplaces = true;
    writeScript("Fixed.cs");
    reloading.fixedUpdate(manager(), 0.6f);

    EXPECT_EQ(count("reload"), 2);
    EXPECT_EQ(count(call("attach", a, "Mover")), 2);
    EXPECT_EQ(count(call("start", a, "Mover")), 2);
  }

  TEST_F(ScriptSystemHotReloadTest, AFailedReloadKeepsTheInstancesAndTriesAgainOnTheNextInterval)
  {
    const auto a = addScriptedObject("A", { "Mover" });
    reloading.start(manager());
    state.reloadThrows = true;
    writeScript("Broken.cs");

    reloading.fixedUpdate(manager(), 0.6f);

    EXPECT_EQ(count("reload"), 1);
    EXPECT_EQ(count(call("attach", a, "Mover")), 1);
    EXPECT_EQ(count(call("detach", a, "Mover")), 0);
    EXPECT_EQ(count(call("fixed", a, "Mover")), 1);

    reloading.fixedUpdate(manager(), 0.6f);

    EXPECT_EQ(count("reload"), 2);
    EXPECT_EQ(count(call("attach", a, "Mover")), 1);
  }

  class StoppedSceneReloadTest : public ScriptSystemHotReloadTest {
  protected:
    std::shared_ptr<SceneAsset> stoppedScene = std::make_shared<SceneAsset>(
      uuids::uuid::from_string("99999999-9999-9999-9999-999999999999").value(), "Stopped", scene.componentRegistry);

    std::shared_ptr<Script> addScriptToStoppedScene(const std::string& className)
    {
      const auto object = std::make_shared<Object>("Scripted");
      stoppedScene->getObjectManager()->addObject(object);

      return addScript(object, className);
    }
  };

  TEST_F(StoppedSceneReloadTest, AChangeIsCompiledOnlyAfterTheThrottleInterval)
  {
    writeScript("Existing.cs");
    reloading.warmUp();

    writeScript("Added.cs");

    EXPECT_FALSE(reloading.pollScriptChanges(*stoppedScene, 0.1f));
    EXPECT_EQ(count("reload"), 0);

    EXPECT_TRUE(reloading.pollScriptChanges(*stoppedScene, 0.5f));
    EXPECT_EQ(count("reload"), 1);
  }

  TEST_F(StoppedSceneReloadTest, ANewlyCompiledClassIsAttachedWithoutStartAndExposesItsFields)
  {
    state.exposedByClass["Fresh"] = exposedFieldsJson;
    state.unresolvedClasses.insert("Fresh");
    state.floats["speed"] = 4.5f;
    const auto script = addScriptToStoppedScene("Fresh");
    reloading.warmUp();
    reloading.attachAll(*stoppedScene);
    ASSERT_TRUE(script->getFields().empty());

    state.unresolvedClasses.clear();
    writeScript("Fresh.cs");

    ASSERT_TRUE(reloading.pollScriptChanges(*stoppedScene, 0.6f));

    const auto speed = findField(script, "speed");
    ASSERT_FALSE(speed.is_null());
    EXPECT_FLOAT_EQ(speed.at("value").get<float>(), 4.5f);
    EXPECT_EQ(script->getFields().size(), 4u);
    EXPECT_EQ(std::ranges::count_if(state.calls, [](const std::string& entry) {
      return entry.starts_with("start|") || entry.starts_with("fixed|") || entry.starts_with("variable|");
    }), 0);
  }

  TEST_F(StoppedSceneReloadTest, NothingHappensWhenTheSourcesDidNotChange)
  {
    writeScript("Existing.cs");
    addScriptToStoppedScene("Mover");
    reloading.warmUp();
    reloading.attachAll(*stoppedScene);
    const auto callsBefore = state.calls.size();

    EXPECT_FALSE(reloading.pollScriptChanges(*stoppedScene, 0.6f));
    EXPECT_FALSE(reloading.pollScriptChanges(*stoppedScene, 0.6f));

    EXPECT_EQ(count("reload"), 0);
    EXPECT_EQ(state.calls.size(), callsBefore);

    writeScript("Added.cs");

    EXPECT_TRUE(reloading.pollScriptChanges(*stoppedScene, 0.6f));
  }

  TEST_F(StoppedSceneReloadTest, ACompileErrorReportsNoReplacementAndIsNotRetried)
  {
    addScriptToStoppedScene("Mover");
    reloading.warmUp();
    state.reloadReplaces = false;
    writeScript("Broken.cs");

    EXPECT_FALSE(reloading.pollScriptChanges(*stoppedScene, 0.6f));
    EXPECT_FALSE(reloading.pollScriptChanges(*stoppedScene, 0.6f));

    EXPECT_EQ(count("reload"), 1);
  }

  TEST_F(StoppedSceneReloadTest, TheRunningPathStillReloadsAndStartsTheInstancesAgain)
  {
    const auto a = addScriptedObject("A", { "Mover" });
    reloading.start(manager());
    writeScript("Added.cs");

    reloading.fixedUpdate(manager(), 0.6f);

    EXPECT_EQ(count("reload"), 1);
    EXPECT_EQ(count(call("start", a, "Mover")), 2);
  }

  TEST_F(StoppedSceneReloadTest, AFailingRuntimeFactoryIsNotRetriedByThePoll)
  {
    int attempts = 0;
    ScriptSystem failing{ [&attempts]() -> std::unique_ptr<ScriptRuntime>
    {
      ++attempts;
      throw std::runtime_error("init failed");
    }, scriptsDir };

    for (int i = 0; i < 5; ++i)
    {
      EXPECT_FALSE(failing.pollScriptChanges(*stoppedScene, 0.6f));
    }

    EXPECT_EQ(attempts, 0);

    reloading.warmUp();
    writeScript("Added.cs");

    EXPECT_TRUE(reloading.pollScriptChanges(*stoppedScene, 0.6f));
  }

  TEST_F(StoppedSceneReloadTest, AHugeDtBanksNoMoreThanOneInterval)
  {
    reloading.warmUp();
    writeScript("One.cs");

    EXPECT_TRUE(reloading.pollScriptChanges(*stoppedScene, 1.0e9f));
    writeScript("Two.cs");

    EXPECT_FALSE(reloading.pollScriptChanges(*stoppedScene, 0.1f));
    EXPECT_EQ(count("reload"), 1);
    EXPECT_TRUE(reloading.pollScriptChanges(*stoppedScene, 0.5f));
    EXPECT_EQ(count("reload"), 2);
  }

  TEST_F(StoppedSceneReloadTest, AMissingScriptDirectoryDoesNotThrow)
  {
    std::filesystem::remove_all(scriptsDir);
    ScriptSystem missing{ factory(), scriptsDir / "nope" };
    missing.warmUp();

    bool replaced = true;
    EXPECT_NO_THROW(replaced = missing.pollScriptChanges(*stoppedScene, 0.6f));
    EXPECT_FALSE(replaced);
  }

  TEST_F(StoppedSceneReloadTest, StartingAfterAStoppedReloadStartsEachScriptOnceAndDeliversLoadedOnce)
  {
    const auto object = std::make_shared<Object>("Scripted");
    stoppedScene->getObjectManager()->addObject(object);
    addScript(object, "Mover");
    ASSERT_TRUE(stoppedScene->addScript(std::make_shared<Script>("Director")));
    reloading.warmUp();
    writeScript("Added.cs");
    ASSERT_TRUE(reloading.pollScriptChanges(*stoppedScene, 0.6f));

    reloading.start(*stoppedScene);

    const auto sceneId = uuids::to_string(stoppedScene->getUUID());
    EXPECT_EQ(count(call("start", object, "Mover")), 1);
    EXPECT_EQ(count(describe("start", sceneId.c_str(), "Director")), 1);
    ASSERT_EQ(state.sceneEvents.size(), 1u);
    EXPECT_EQ(state.sceneEvents.front().event, static_cast<int>(SceneEvent::loaded));
  }

  TEST_F(StoppedSceneReloadTest, AFieldEditedWhileStoppedSurvivesTheReload)
  {
    state.exposedByClass["Mover"] = exposedFieldsJson;
    const auto object = std::make_shared<Object>("Scripted");
    stoppedScene->getObjectManager()->addObject(object);
    const auto script = addScript(object, "Mover");
    reloading.warmUp();
    reloading.attachAll(*stoppedScene);

    const nlohmann::json edited = nlohmann::json::array({
      { { "name", "speed" }, { "type", "float" }, { "value", 7.5 } } });
    script->setFields(edited);
    reloading.applyScriptFieldEdit(object->getUUID(), "Mover", edited);
    writeScript("Added.cs");

    ASSERT_TRUE(reloading.pollScriptChanges(*stoppedScene, 0.6f));

    EXPECT_FLOAT_EQ(findField(script, "speed").at("value").get<float>(), 7.5f);
  }

  TEST_F(StoppedSceneReloadTest, RemovingTheScriptAfterAStoppedReloadDetachesItOnTheNextAttachAll)
  {
    const auto object = std::make_shared<Object>("Scripted");
    stoppedScene->getObjectManager()->addObject(object);
    const auto script = addScript(object, "Mover");
    reloading.warmUp();
    writeScript("Added.cs");
    ASSERT_TRUE(reloading.pollScriptChanges(*stoppedScene, 0.6f));
    ASSERT_EQ(count(call("attach", object, "Mover")), 1);
    ASSERT_EQ(count(call("detach", object, "Mover")), 0);

    object->removeComponent(script);
    reloading.attachAll(*stoppedScene);

    EXPECT_EQ(count(call("detach", object, "Mover")), 1);
  }
}
