#include "ScriptSystemTestFixtures.h"

#include "ComponentRegistry.h"
#include "scenes/SceneAsset.h"

#include <Log.h>
#include <RingBufferSink.h>
#include <string>
#include <vector>

namespace {
  using namespace scriptSystemFixtures;
  using Calls = std::vector<std::string>;

  const auto mainSceneUUID = uuids::uuid::from_string("77777777-7777-7777-7777-777777777777").value();
  const auto otherSceneUUID = uuids::uuid::from_string("88888888-8888-8888-8888-888888888888").value();

  class SceneScriptSystemTest : public ScriptSystemTest {
  protected:
    std::shared_ptr<SceneAsset> mainScene =
      std::make_shared<SceneAsset>(mainSceneUUID, "Main", scene.componentRegistry);
    std::shared_ptr<RingBufferSink> sink = std::make_shared<RingBufferSink>();

    void SetUp() override
    {
      Log::addSink(sink);
    }

    void TearDown() override
    {
      Log::removeSink(sink);
      ScriptSystemTest::TearDown();
    }

    static std::shared_ptr<Script> addSceneScript(SceneAsset& target, const std::string& className,
                                                  const nlohmann::json& fields = nlohmann::json::array())
    {
      const auto script = std::make_shared<Script>(className);
      script->setFields(fields);
      EXPECT_TRUE(target.addScript(script));

      return script;
    }

    static std::shared_ptr<Object> addObjectTo(SceneAsset& target, const std::string& name,
                                               const std::vector<std::string>& classNames)
    {
      const auto object = std::make_shared<Object>(name);
      target.getObjectManager()->addObject(object);
      for (const auto& className : classNames)
      {
        addScript(object, className);
      }

      return object;
    }

    [[nodiscard]] static std::string sceneCall(const char* verb, const uuids::uuid& sceneUUID,
                                               const char* className)
    {
      return describe(verb, uuids::to_string(sceneUUID).c_str(), className);
    }

    [[nodiscard]] static std::string mainCall(const char* verb, const char* className)
    {
      return sceneCall(verb, mainSceneUUID, className);
    }

    [[nodiscard]] Calls callsOf(const std::string& verb) const
    {
      Calls matching;
      for (const auto& entry : state.calls)
      {
        if (entry.starts_with(verb + "|"))
        {
          matching.push_back(entry);
        }
      }

      return matching;
    }

    [[nodiscard]] std::ptrdiff_t warningsAbout(const std::string& text) const
    {
      return std::ranges::count_if(sink->snapshot(), [&text](const LogEntry& entry)
      {
        return entry.level == LogLevel::warn && entry.message.find(text) != std::string::npos;
      });
    }
  };

  TEST_F(SceneScriptSystemTest, StartAttachesAndStartsSceneScriptsInListOrderBeforeObjectScripts)
  {
    addSceneScript(*mainScene, "Alpha");
    addSceneScript(*mainScene, "Beta");
    const auto a = addObjectTo(*mainScene, "A", { "Mover" });

    scriptSystem.start(*mainScene);

    EXPECT_EQ(callsOf("start"), (Calls{ mainCall("start", "Alpha"), mainCall("start", "Beta"),
                                        call("start", a, "Mover") }));
    EXPECT_EQ(callsOf("attachScene"), (Calls{ mainCall("attachScene", "Alpha"), mainCall("attachScene", "Beta") }));
    EXPECT_LT(indexOf(mainCall("attachScene", "Alpha")), indexOf(mainCall("start", "Alpha")));
  }

  TEST_F(SceneScriptSystemTest, EachTickRunsSceneScriptsBeforeObjectScriptsInListOrder)
  {
    addSceneScript(*mainScene, "Alpha");
    addSceneScript(*mainScene, "Beta");
    const auto a = addObjectTo(*mainScene, "A", { "Mover" });
    scriptSystem.start(*mainScene);

    scriptSystem.variableUpdate(*mainScene);
    scriptSystem.fixedUpdate(*mainScene, 0.02f);

    EXPECT_EQ(callsOf("variable"), (Calls{ mainCall("variable", "Alpha"), mainCall("variable", "Beta"),
                                           call("variable", a, "Mover") }));
    EXPECT_EQ(callsOf("fixed"), (Calls{ mainCall("fixed", "Alpha"), mainCall("fixed", "Beta"),
                                        call("fixed", a, "Mover") }));
  }

  TEST_F(SceneScriptSystemTest, StopStopsObjectScriptsThenSceneScriptsAndLeavesNothingAttached)
  {
    addSceneScript(*mainScene, "Alpha");
    addSceneScript(*mainScene, "Beta");
    const auto a = addObjectTo(*mainScene, "A", { "Mover" });
    scriptSystem.start(*mainScene);

    scriptSystem.stop(*mainScene);

    EXPECT_EQ(callsOf("stop"), (Calls{ call("stop", a, "Mover"), mainCall("stop", "Alpha"),
                                       mainCall("stop", "Beta") }));
    EXPECT_EQ(count(mainCall("detach", "Alpha")), 1);
    EXPECT_EQ(count(mainCall("detach", "Beta")), 1);
    EXPECT_EQ(count(call("detach", a, "Mover")), 1);

    state.calls.clear();
    scriptSystem.variableUpdate(*mainScene);
    scriptSystem.stop(*mainScene);

    EXPECT_TRUE(state.calls.empty());
  }

  TEST_F(SceneScriptSystemTest, ASceneScriptRunsItsWholeLifecycleInASceneWithNoObjects)
  {
    ASSERT_TRUE(mainScene->getObjectManager()->getAllObjects().empty());
    addSceneScript(*mainScene, "Alpha");

    scriptSystem.start(*mainScene);
    scriptSystem.variableUpdate(*mainScene);
    scriptSystem.fixedUpdate(*mainScene, 0.02f);
    scriptSystem.stop(*mainScene);

    EXPECT_EQ(state.calls, (Calls{ mainCall("attachScene", "Alpha"), mainCall("start", "Alpha"),
                                   mainCall("variable", "Alpha"), mainCall("fixed", "Alpha"),
                                   mainCall("stop", "Alpha"), mainCall("detach", "Alpha") }));
  }

  TEST_F(SceneScriptSystemTest, SceneScriptsAttachThroughTheSceneEntryPointAndObjectScriptsThroughTheObjectOne)
  {
    addSceneScript(*mainScene, "Alpha");
    const auto a = addObjectTo(*mainScene, "A", { "Mover" });

    scriptSystem.start(*mainScene);

    EXPECT_EQ(count(mainCall("attachScene", "Alpha")), 1);
    EXPECT_EQ(count(mainCall("attach", "Alpha")), 0);
    EXPECT_EQ(count(call("attach", a, "Mover")), 1);
    EXPECT_EQ(count(describe("attachScene", id(a).c_str(), "Mover")), 0);
  }

  TEST_F(SceneScriptSystemTest, AFixedUpdateAttachesAndStartsASceneScriptAddedToARunningScene)
  {
    scriptSystem.start(*mainScene);
    ASSERT_EQ(count(mainCall("attachScene", "Alpha")), 0);

    addSceneScript(*mainScene, "Alpha");
    scriptSystem.variableUpdate(*mainScene);
    EXPECT_EQ(count(mainCall("variable", "Alpha")), 0);

    scriptSystem.fixedUpdate(*mainScene, 0.02f);
    scriptSystem.variableUpdate(*mainScene);

    EXPECT_EQ(count(mainCall("attachScene", "Alpha")), 1);
    EXPECT_EQ(count(mainCall("start", "Alpha")), 1);
    EXPECT_EQ(count(mainCall("fixed", "Alpha")), 1);
    EXPECT_EQ(count(mainCall("variable", "Alpha")), 1);
  }

  TEST_F(SceneScriptSystemTest, AnUnresolvedSceneScriptIsWarnedOnceNeverTickedAndKeepsItsSavedFields)
  {
    state.unresolvedClasses.insert("Ghost");
    state.exposedByClass["Alpha"] = exposedFieldsJson;
    state.floats["speed"] = 9.0f;
    const auto saved = nlohmann::json::parse(R"([{"name":"speed","type":"float","value":3.0}])");
    const auto ghost = addSceneScript(*mainScene, "Ghost", saved);
    addSceneScript(*mainScene, "Alpha");

    scriptSystem.start(*mainScene);
    for (int i = 0; i < 3; ++i)
    {
      scriptSystem.variableUpdate(*mainScene);
      scriptSystem.fixedUpdate(*mainScene, 0.02f);
    }

    scriptSystem.syncFieldsToData(*mainScene);

    EXPECT_EQ(count(mainCall("attachScene", "Ghost")), 1);
    EXPECT_EQ(warningsAbout("Scene script 'Ghost' in scene 'Main'"), 1);
    EXPECT_EQ(count(mainCall("start", "Ghost")), 0);
    EXPECT_EQ(count(mainCall("fixed", "Ghost")), 0);
    EXPECT_EQ(count(mainCall("variable", "Ghost")), 0);
    EXPECT_EQ(ghost->getFields(), saved);
    EXPECT_EQ(count(mainCall("fixed", "Alpha")), 3);
    EXPECT_FLOAT_EQ(findField(mainScene->findScript("Alpha"), "speed").at("value").get<float>(), 9.0f);
  }

  TEST_F(SceneScriptSystemTest, AReplacedUnresolvedSceneScriptIsRetriedAndWarnedAgain)
  {
    state.unresolvedClasses.insert("Ghost");
    addSceneScript(*mainScene, "Ghost");
    scriptSystem.start(*mainScene);
    scriptSystem.fixedUpdate(*mainScene, 0.02f);
    ASSERT_EQ(count(mainCall("attachScene", "Ghost")), 1);

    ASSERT_TRUE(mainScene->removeScript("Ghost"));
    addSceneScript(*mainScene, "Ghost");
    state.unresolvedClasses.clear();
    scriptSystem.fixedUpdate(*mainScene, 0.02f);

    EXPECT_EQ(count(mainCall("attachScene", "Ghost")), 2);
    EXPECT_EQ(count(mainCall("start", "Ghost")), 1);
    EXPECT_EQ(count(mainCall("fixed", "Ghost")), 1);
    EXPECT_EQ(warningsAbout("Scene script 'Ghost' in scene 'Main'"), 1);
  }

  TEST_F(SceneScriptSystemTest, RemovingASceneScriptStopsAndDetachesItWhileObjectScriptsKeepTicking)
  {
    addSceneScript(*mainScene, "Alpha");
    addSceneScript(*mainScene, "Beta");
    const auto a = addObjectTo(*mainScene, "A", { "Mover" });
    scriptSystem.start(*mainScene);

    ASSERT_TRUE(mainScene->removeScript("Beta"));
    scriptSystem.fixedUpdate(*mainScene, 0.02f);

    EXPECT_EQ(count(mainCall("stop", "Beta")), 1);
    EXPECT_EQ(count(mainCall("detach", "Beta")), 1);
    EXPECT_LT(indexOf(mainCall("stop", "Beta")), indexOf(mainCall("detach", "Beta")));
    EXPECT_EQ(count(mainCall("fixed", "Beta")), 0);

    EXPECT_EQ(count(mainCall("fixed", "Alpha")), 1);
    EXPECT_EQ(count(call("fixed", a, "Mover")), 1);
    EXPECT_EQ(count(mainCall("detach", "Alpha")), 0);
    EXPECT_EQ(count(call("detach", a, "Mover")), 0);
    EXPECT_EQ(count(call("stop", a, "Mover")), 0);
  }

  TEST_F(SceneScriptSystemTest, AttachAllSweepsARemovedSceneScriptWhileTheSceneIsStopped)
  {
    addSceneScript(*mainScene, "Alpha");
    scriptSystem.attachAll(*mainScene);
    ASSERT_EQ(count(mainCall("attachScene", "Alpha")), 1);

    ASSERT_TRUE(mainScene->removeScript("Alpha"));
    scriptSystem.attachAll(*mainScene);

    EXPECT_EQ(count(mainCall("detach", "Alpha")), 1);
    EXPECT_EQ(count(mainCall("stop", "Alpha")), 0);
    EXPECT_EQ(count(mainCall("start", "Alpha")), 0);
  }

  TEST_F(SceneScriptSystemTest, RestartingAfterTheSceneRebuildsItsScriptEntriesAttachesAndStartsFresh)
  {
    addSceneScript(*mainScene, "Alpha");
    addObjectTo(*mainScene, "A", { "Mover" });
    mainScene->start();
    scriptSystem.start(*mainScene);
    scriptSystem.fixedUpdate(*mainScene, 0.02f);
    const auto firstEntry = mainScene->findScript("Alpha");

    scriptSystem.stop(*mainScene);
    mainScene->stop();
    ASSERT_NE(mainScene->findScript("Alpha"), firstEntry);
    ASSERT_EQ(count(mainCall("detach", "Alpha")), 1);

    state.calls.clear();
    mainScene->start();
    scriptSystem.start(*mainScene);
    scriptSystem.fixedUpdate(*mainScene, 0.02f);

    EXPECT_EQ(count(mainCall("attachScene", "Alpha")), 1);
    EXPECT_EQ(count(mainCall("start", "Alpha")), 1);
    EXPECT_EQ(count(mainCall("fixed", "Alpha")), 1);
    EXPECT_EQ(count(mainCall("stop", "Alpha")), 0);
    EXPECT_EQ(count(mainCall("detach", "Alpha")), 0);
  }

  TEST_F(SceneScriptSystemTest, ARebuiltEntryWithoutAStopOrphansTheOldInstanceInsteadOfInheritingIt)
  {
    addSceneScript(*mainScene, "Alpha");
    scriptSystem.start(*mainScene);

    ASSERT_TRUE(mainScene->removeScript("Alpha"));
    addSceneScript(*mainScene, "Alpha");
    scriptSystem.fixedUpdate(*mainScene, 0.02f);

    EXPECT_EQ(count(mainCall("stop", "Alpha")), 1);
    EXPECT_EQ(count(mainCall("detach", "Alpha")), 1);
    EXPECT_EQ(count(mainCall("attachScene", "Alpha")), 2);
    EXPECT_EQ(count(mainCall("start", "Alpha")), 2);
    EXPECT_LT(indexOf(mainCall("detach", "Alpha")), indexOf(mainCall("fixed", "Alpha")));
  }

  TEST_F(SceneScriptSystemTest, StoppingASceneLeavesNothingOfItAttachedAndAnotherSceneNeverTicksIt)
  {
    SceneAsset other(otherSceneUUID, "Other", scene.componentRegistry);
    addSceneScript(*mainScene, "Alpha");
    addSceneScript(other, "Beta");
    scriptSystem.start(*mainScene);
    scriptSystem.variableUpdate(*mainScene);
    scriptSystem.fixedUpdate(*mainScene, 0.02f);
    scriptSystem.stop(*mainScene);
    ASSERT_EQ(count(mainCall("detach", "Alpha")), 1);
    ASSERT_EQ(count(mainCall("fixed", "Alpha")), 1);

    scriptSystem.start(other);
    scriptSystem.variableUpdate(other);
    scriptSystem.fixedUpdate(other, 0.02f);

    EXPECT_EQ(count(sceneCall("fixed", otherSceneUUID, "Beta")), 1);
    EXPECT_EQ(count(sceneCall("variable", otherSceneUUID, "Beta")), 1);
    EXPECT_EQ(count(mainCall("fixed", "Alpha")), 1);
    EXPECT_EQ(count(mainCall("variable", "Alpha")), 1);
    EXPECT_EQ(count(mainCall("stop", "Alpha")), 1);
  }

  TEST_F(SceneScriptSystemTest, TickingADifferentSceneStopsAndDetachesTheOldScenesScriptsOnce)
  {
    SceneAsset other(otherSceneUUID, "Other", scene.componentRegistry);
    addSceneScript(*mainScene, "Alpha");
    addSceneScript(other, "Beta");
    scriptSystem.start(*mainScene);

    scriptSystem.fixedUpdate(other, 0.02f);
    scriptSystem.fixedUpdate(other, 0.02f);

    EXPECT_EQ(count(mainCall("stop", "Alpha")), 1);
    EXPECT_EQ(count(mainCall("detach", "Alpha")), 1);
    EXPECT_EQ(count(mainCall("fixed", "Alpha")), 0);
    EXPECT_EQ(count(sceneCall("fixed", otherSceneUUID, "Beta")), 2);
  }

  TEST_F(SceneScriptSystemTest, TheObjectOnlyOverloadsNeverStopTickOrDetachASceneScript)
  {
    addSceneScript(*mainScene, "Alpha");
    const auto a = addObjectTo(*mainScene, "A", { "Mover" });
    const auto spinner = addScript(a, "Spinner");
    scriptSystem.start(*mainScene);
    state.calls.clear();

    a->removeComponent(spinner);
    scriptSystem.fixedUpdate(*mainScene->getObjectManager(), 0.02f);
    scriptSystem.variableUpdate(*mainScene->getObjectManager());
    scriptSystem.attachAll(*mainScene->getObjectManager());

    EXPECT_EQ(count(call("stop", a, "Spinner")), 1);
    EXPECT_EQ(count(call("detach", a, "Spinner")), 1);
    EXPECT_EQ(count(call("fixed", a, "Mover")), 1);
    EXPECT_EQ(count(mainCall("stop", "Alpha")), 0);
    EXPECT_EQ(count(mainCall("detach", "Alpha")), 0);
    EXPECT_EQ(count(mainCall("fixed", "Alpha")), 0);
    EXPECT_EQ(count(mainCall("variable", "Alpha")), 0);

    scriptSystem.stop(*mainScene->getObjectManager());

    EXPECT_EQ(count(call("stop", a, "Mover")), 1);
    EXPECT_EQ(count(mainCall("stop", "Alpha")), 0);

    scriptSystem.stop(*mainScene);

    EXPECT_EQ(count(mainCall("stop", "Alpha")), 1);
    EXPECT_EQ(count(mainCall("detach", "Alpha")), 1);
  }

  TEST_F(SceneScriptSystemTest, SyncFieldsToDataRefreshesAHealthySceneScriptAndSkipsAnUnhealthyOne)
  {
    state.exposedByClass["Alpha"] = exposedFieldsJson;
    state.exposedByClass["Beta"] = exposedFieldsJson;
    state.unhealthyClasses.insert("Beta");
    const auto saved = nlohmann::json::parse(R"([{"name":"speed","type":"float","value":1.0}])");
    const auto alpha = addSceneScript(*mainScene, "Alpha", saved);
    const auto beta = addSceneScript(*mainScene, "Beta", saved);
    scriptSystem.start(*mainScene);
    state.floats["speed"] = 7.5f;

    scriptSystem.syncFieldsToData(*mainScene);

    ASSERT_EQ(alpha->getFields().size(), 4u);
    EXPECT_FLOAT_EQ(findField(alpha, "speed").at("value").get<float>(), 7.5f);
    EXPECT_EQ(beta->getFields(), saved);
  }

  TEST_F(SceneScriptSystemTest, SyncFieldsToDataAlsoRefreshesTheScenesObjectScripts)
  {
    state.exposedByClass["Mover"] = exposedFieldsJson;
    const auto a = addObjectTo(*mainScene, "A", {});
    const auto mover = addScript(a, "Mover");
    scriptSystem.start(*mainScene);
    state.ints["count"] = 12;

    scriptSystem.syncFieldsToData(*mainScene);

    EXPECT_EQ(findField(mover, "count").at("value").get<int>(), 12);
  }

  TEST_F(SceneScriptSystemTest, ApplySceneScriptFieldEditReachesTheLiveInstanceOnlyWhenAttached)
  {
    state.exposedByClass["Alpha"] = exposedFieldsJson;
    addSceneScript(*mainScene, "Alpha");
    const auto edit = nlohmann::json::parse(R"([{"name":"speed","type":"float","value":4.5}])");

    scriptSystem.applySceneScriptFieldEdit(mainSceneUUID, "Alpha", edit);
    EXPECT_EQ(count("setFloat|speed"), 0);

    scriptSystem.start(*mainScene);
    state.calls.clear();

    scriptSystem.applySceneScriptFieldEdit(mainSceneUUID, "Alpha", edit);
    EXPECT_EQ(count("setFloat|speed"), 1);
    EXPECT_FLOAT_EQ(state.floats["speed"], 4.5f);

    state.calls.clear();
    scriptSystem.applySceneScriptFieldEdit(mainSceneUUID, "Unknown", edit);
    scriptSystem.applySceneScriptFieldEdit(otherSceneUUID, "Alpha", edit);

    EXPECT_TRUE(state.calls.empty());
  }

  class SceneScriptHotReloadTest : public SceneScriptSystemTest {
  protected:
    std::filesystem::path scriptsDir;
    ScriptSystem reloading{ factory(), makeDir() };

    ~SceneScriptHotReloadTest() override
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
      scriptsDir = std::filesystem::temp_directory_path() / (std::string("ecs3d_scene_scripts_") + info->name());
      std::error_code ec;
      std::filesystem::remove_all(scriptsDir, ec);
      std::filesystem::create_directories(scriptsDir);

      return scriptsDir;
    }
  };

  TEST_F(SceneScriptHotReloadTest, AReloadReattachesAndRestartsHealthySceneScriptsWithTheirRefreshedFields)
  {
    state.exposedByClass["Alpha"] = exposedFieldsJson;
    const auto alpha = addSceneScript(*mainScene, "Alpha");
    writeScript("Existing.cs");
    reloading.start(*mainScene);
    ASSERT_EQ(count(mainCall("attachScene", "Alpha")), 1);
    state.floats["speed"] = 6.0f;

    writeScript("Added.cs");
    reloading.fixedUpdate(*mainScene, 0.6f);

    EXPECT_EQ(count("reload"), 1);
    EXPECT_EQ(count(mainCall("attachScene", "Alpha")), 2);
    EXPECT_EQ(count(mainCall("start", "Alpha")), 2);
    EXPECT_EQ(count(mainCall("fixed", "Alpha")), 1);
    EXPECT_FLOAT_EQ(findField(alpha, "speed").at("value").get<float>(), 6.0f);
  }

  TEST_F(SceneScriptHotReloadTest, AReloadGivesAnUnresolvedSceneScriptAFreshAttempt)
  {
    state.unresolvedClasses.insert("Ghost");
    addSceneScript(*mainScene, "Ghost");
    writeScript("Existing.cs");
    reloading.start(*mainScene);
    reloading.fixedUpdate(*mainScene, 0.02f);
    ASSERT_EQ(count(mainCall("attachScene", "Ghost")), 1);
    ASSERT_EQ(count(mainCall("start", "Ghost")), 0);

    state.unresolvedClasses.clear();
    writeScript("Added.cs");
    reloading.fixedUpdate(*mainScene, 0.6f);

    EXPECT_EQ(count("reload"), 1);
    EXPECT_EQ(count(mainCall("attachScene", "Ghost")), 2);
    EXPECT_EQ(count(mainCall("start", "Ghost")), 1);
    EXPECT_EQ(count(mainCall("fixed", "Ghost")), 1);
  }
}
