#ifndef SCRIPTSYSTEMTESTFIXTURES_H
#define SCRIPTSYSTEMTESTFIXTURES_H

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
#include <functional>
#include <fstream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include <uuid.h>

namespace scriptSystemFixtures {
  // One onSceneEvent call. Kept out of RuntimeState::calls so a suite that pins that list exactly is unaffected;
  // callsBefore is calls.size() at the moment of the event, to order it against the lifecycle calls.
  struct SceneEventCall {
    std::string scene;
    std::string className;
    int event = 0;
    std::string object;
    int slot = 0;
    std::size_t callsBefore = 0;

    [[nodiscard]] bool operator==(const SceneEventCall& other) const
    {
      return scene == other.scene && className == other.className && event == other.event &&
             object == other.object && slot == other.slot;
    }
  };

  // Everything the fake runtime was asked to do, in order, plus the values it hands back. Owned by the
  // test so it outlives the runtime ScriptSystem holds.
  struct RuntimeState {
    std::vector<std::string> calls;
    std::vector<SceneEventCall> sceneEvents;
    // Runs inside onSceneEvent, standing in for a handler's side effects.
    std::function<void(const SceneEventCall&)> sceneEventHook;
    // Run inside start/stop (uuid, class name), standing in for a script that spawns objects.
    std::function<void(const std::string&, const std::string&)> startHook;
    std::function<void(const std::string&, const std::string&)> stopHook;
    std::map<std::string, std::string> exposedByClass;
    std::map<std::string, float> floats;
    std::map<std::string, int> ints;
    std::map<std::string, bool> bools;
    std::map<std::string, std::string> strings;
    std::map<std::string, glm::vec3> vectors;
    bool reloadThrows = false;
    bool reloadReplaces = true;
    std::optional<std::vector<ScriptClassInfo>> classNames;
    // Classes the fake cannot instantiate, and classes it reports as faulted.
    std::set<std::string> unresolvedClasses;
    std::set<std::string> unhealthyClasses;
    int created = 0;
  };

  inline std::string describe(const char* verb, const char* uuid, const char* className)
  {
    return std::string(verb) + "|" + uuid + "|" + className;
  }

  class RecordingRuntime final : public ScriptRuntime {
  public:
    explicit RecordingRuntime(RuntimeState& state)
      : m_state(state)
    {}

    [[nodiscard]] bool reloadScripts() const override
    {
      m_state.calls.emplace_back("reload");

      if (m_state.reloadThrows)
      {
        throw std::runtime_error("compile failed");
      }

      return m_state.reloadReplaces;
    }

    [[nodiscard]] std::optional<std::vector<ScriptClassInfo>> scriptClassNames() const override
    {
      return m_state.classNames;
    }

    [[nodiscard]] bool attachScript(const char* uuid, const char* className) const override
    {
      m_state.calls.push_back(describe("attach", uuid, className));

      return !m_state.unresolvedClasses.contains(className);
    }

    [[nodiscard]] bool attachSceneScript(const char* uuid, const char* className) const override
    {
      m_state.calls.push_back(describe("attachScene", uuid, className));

      return !m_state.unresolvedClasses.contains(className);
    }

    [[nodiscard]] bool isHealthy(const char*, const char* className) const override
    {
      return !m_state.unhealthyClasses.contains(className);
    }

    void detachScript(const char* uuid, const char* className) const override
    {
      m_state.calls.push_back(describe("detach", uuid, className));
    }

    void start(const char* uuid, const char* className) const override
    {
      m_state.calls.push_back(describe("start", uuid, className));

      if (m_state.startHook)
      {
        m_state.startHook(uuid, className);
      }
    }

    void stop(const char* uuid, const char* className) const override
    {
      m_state.calls.push_back(describe("stop", uuid, className));

      if (m_state.stopHook)
      {
        m_state.stopHook(uuid, className);
      }
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

    void onSceneEvent(const char* sceneUuid, const char* className, int event, const char* objectUuid,
                      int slot) const override
    {
      m_state.sceneEvents.push_back({ sceneUuid, className, event, objectUuid, slot, m_state.calls.size() });

      if (m_state.sceneEventHook)
      {
        m_state.sceneEventHook(m_state.sceneEvents.back());
      }
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

    [[nodiscard]] std::string getFieldString(const char*, const char*, const char* fieldName) const override
    {
      return m_state.strings[fieldName];
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

    void setFieldString(const char*, const char*, const char* fieldName, const char* value) const override
    {
      m_state.calls.push_back(std::string("setString|") + fieldName);
      m_state.strings[fieldName] = value;
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

  const char* const exposedStringFieldJson = R"([{"name":"label","type":"string"},{"name":"count","type":"int"}])";

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

  inline nlohmann::json findField(const std::shared_ptr<Script>& script, const std::string& name)
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
}

#endif //SCRIPTSYSTEMTESTFIXTURES_H
