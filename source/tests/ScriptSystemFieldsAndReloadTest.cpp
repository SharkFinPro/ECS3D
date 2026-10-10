#include "ScriptSystemTestFixtures.h"

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
}
