#include <gtest/gtest.h>

#include "ScriptSystem.h"
#include "TestScene.h"
#include "bindings/BindingContext.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Script.h"

#include <glm/vec3.hpp>
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include <uuid.h>

namespace {
  // Everything the fake runtime was asked to do, in order, plus the values it hands back. Owned by the
  // test so it outlives the runtime ScriptSystem holds.
  struct RuntimeState {
    std::vector<std::string> calls;
    std::map<std::string, std::string> exposedByClass;
    std::map<std::string, float> floats;
    std::map<std::string, int> ints;
    std::map<std::string, bool> bools;
    std::map<std::string, glm::vec3> vectors;
    bool reloadThrows = false;
    int created = 0;
  };

  std::string describe(const char* verb, const char* uuid, const char* className)
  {
    return std::string(verb) + "|" + uuid + "|" + className;
  }

  class RecordingRuntime final : public ScriptRuntime {
  public:
    explicit RecordingRuntime(RuntimeState& state)
      : m_state(state)
    {}

    void reloadScripts() const override
    {
      m_state.calls.emplace_back("reload");

      if (m_state.reloadThrows)
      {
        throw std::runtime_error("compile failed");
      }
    }

    void attachScript(const char* uuid, const char* className) const override
    {
      m_state.calls.push_back(describe("attach", uuid, className));
    }

    void detachScript(const char* uuid, const char* className) const override
    {
      m_state.calls.push_back(describe("detach", uuid, className));
    }

    void start(const char* uuid, const char* className) const override
    {
      m_state.calls.push_back(describe("start", uuid, className));
    }

    void stop(const char* uuid, const char* className) const override
    {
      m_state.calls.push_back(describe("stop", uuid, className));
    }

    void fixedUpdate(const char* uuid, const char* className, float) const override
    {
      m_state.calls.push_back(describe("fixed", uuid, className));
    }

    void variableUpdate(const char* uuid, const char* className) const override
    {
      m_state.calls.push_back(describe("variable", uuid, className));
    }

    void onCollision(const char* uuid, const char* className, const char* otherUuid, int event) const override
    {
      m_state.calls.push_back(describe("collision", uuid, className) + "|" + otherUuid + "|" + std::to_string(event));
    }

    [[nodiscard]] std::string getExposedFields(const char*, const char* className) const override
    {
      const auto it = m_state.exposedByClass.find(className);

      return it == m_state.exposedByClass.end() ? "[]" : it->second;
    }

    [[nodiscard]] float getFieldFloat(const char*, const char*, const char* fieldName) const override
    {
      return m_state.floats[fieldName];
    }

    [[nodiscard]] int getFieldInt(const char*, const char*, const char* fieldName) const override
    {
      return m_state.ints[fieldName];
    }

    [[nodiscard]] bool getFieldBool(const char*, const char*, const char* fieldName) const override
    {
      return m_state.bools[fieldName];
    }

    void getFieldVector3(const char*, const char*, const char* fieldName, float& x, float& y, float& z) const override
    {
      const auto value = m_state.vectors[fieldName];
      x = value.x;
      y = value.y;
      z = value.z;
    }

    void setFieldFloat(const char*, const char*, const char* fieldName, float value) const override
    {
      m_state.calls.push_back(std::string("setFloat|") + fieldName);
      m_state.floats[fieldName] = value;
    }

    void setFieldInt(const char*, const char*, const char* fieldName, int value) const override
    {
      m_state.calls.push_back(std::string("setInt|") + fieldName);
      m_state.ints[fieldName] = value;
    }

    void setFieldBool(const char*, const char*, const char* fieldName, bool value) const override
    {
      m_state.calls.push_back(std::string("setBool|") + fieldName);
      m_state.bools[fieldName] = value;
    }

    void setFieldVector3(const char*, const char*, const char* fieldName, float x, float y, float z) const override
    {
      m_state.calls.push_back(std::string("setVector3|") + fieldName);
      m_state.vectors[fieldName] = glm::vec3(x, y, z);
    }

  private:
    RuntimeState& m_state;
  };

  const char* const exposedFieldsJson =
    R"([{"name":"speed","type":"float"},{"name":"count","type":"int"},)"
    R"({"name":"enabled","type":"bool"},{"name":"offset","type":"vector3"}])";

  class ScriptSystemTest : public ::testing::Test {
  protected:
    fixtures::Scene scene = fixtures::makeScene();
    RuntimeState state;

    [[nodiscard]] ScriptSystem::RuntimeFactory factory()
    {
      return [this]() -> std::unique_ptr<ScriptRuntime>
      {
        ++state.created;

        return std::make_unique<RecordingRuntime>(state);
      };
    }

    ScriptSystem scriptSystem{ factory() };

    void TearDown() override
    {
      BindingContext::setObjectManager(nullptr);
    }

    [[nodiscard]] ObjectManager& manager() const
    {
      return *scene.objectManager;
    }

    std::shared_ptr<Object> addScriptedObject(const std::string& name, const std::vector<std::string>& classNames)
    {
      const auto object = fixtures::addObject(scene, name);
      for (const auto& className : classNames)
      {
        addScript(object, className);
      }

      return object;
    }

    static std::shared_ptr<Script> addScript(const std::shared_ptr<Object>& object,
                                             const std::string& className,
                                             const nlohmann::json& fields = nlohmann::json::array())
    {
      const auto script = std::make_shared<Script>(className);
      script->setFields(fields);
      object->addComponent(script);

      return script;
    }

    [[nodiscard]] static std::string id(const std::shared_ptr<Object>& object)
    {
      return uuids::to_string(object->getUUID());
    }

    [[nodiscard]] static std::string call(const char* verb, const std::shared_ptr<Object>& object,
                                          const char* className)
    {
      return describe(verb, id(object).c_str(), className);
    }

    [[nodiscard]] std::ptrdiff_t count(const std::string& entry) const
    {
      return std::ranges::count(state.calls, entry);
    }

    [[nodiscard]] std::ptrdiff_t indexOf(const std::string& entry) const
    {
      const auto it = std::ranges::find(state.calls, entry);

      return it == state.calls.end() ? -1 : it - state.calls.begin();
    }
  };

  nlohmann::json findField(const std::shared_ptr<Script>& script, const std::string& name)
  {
    for (const auto& field : script->getFields())
    {
      if (field.at("name") == name)
      {
        return field;
      }
    }

    return nullptr;
  }

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
