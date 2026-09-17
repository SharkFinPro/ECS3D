#include <gtest/gtest.h>

#include "ScriptFieldEdit.h"

#include <nlohmann/json.hpp>
#include <cstdint>
#include <limits>
#include <string>

namespace {
  nlohmann::json makeField(const std::string& name, const std::string& type, const nlohmann::json& value)
  {
    return {
      { "name", name },
      { "type", type },
      { "value", value }
    };
  }

  std::string reject(const nlohmann::json& field, const std::string& cachedType)
  {
    return scripting::rejectFieldEdit(field, &cachedType);
  }

  TEST(ScriptFieldEditTest, AcceptsWellTypedValues)
  {
    EXPECT_EQ(reject(makeField("speed", "float", 1.5), "float"), "");
    EXPECT_EQ(reject(makeField("count", "int", 3), "int"), "");
    EXPECT_EQ(reject(makeField("enabled", "bool", true), "bool"), "");
    EXPECT_EQ(reject(makeField("offset", "vector3", nlohmann::json::array({ 1.0, 2, -3.5 })), "vector3"), "");
    EXPECT_EQ(reject(makeField("label", "string", "hello"), "string"), "");
  }

  TEST(ScriptFieldEditTest, AcceptsAnIntegralNumberForFloat)
  {
    EXPECT_EQ(reject(makeField("speed", "float", 2), "float"), "");
  }

  TEST(ScriptFieldEditTest, RejectsAnIncompleteEntry)
  {
    nlohmann::json noValue = {
      { "name", "speed" },
      { "type", "float" }
    };
    EXPECT_EQ(reject(noValue, "float"), "field entry is missing name, type or value");

    nlohmann::json noName = {
      { "type", "float" },
      { "value", 1.0 }
    };
    EXPECT_EQ(reject(noName, "float"), "field entry is missing name, type or value");

    nlohmann::json noType = {
      { "name", "speed" },
      { "value", 1.0 }
    };
    EXPECT_EQ(reject(noType, "float"), "field entry is missing name, type or value");
  }

  TEST(ScriptFieldEditTest, RejectsAFieldTheScriptDoesNotExpose)
  {
    EXPECT_EQ(scripting::rejectFieldEdit(makeField("gone", "float", 1.0), nullptr),
              "field 'gone' is not exposed by the script");
  }

  TEST(ScriptFieldEditTest, RejectsATagThatDisagreesWithTheInstance)
  {
    EXPECT_EQ(reject(makeField("offset", "float", 1.0), "vector3"),
              "field 'offset' is vector3 but the edit says float");
  }

  TEST(ScriptFieldEditTest, RejectsAnUnsupportedTag)
  {
    EXPECT_EQ(reject(makeField("model", "asset", "cube.obj"), "asset"),
              "field 'model' has unsupported type asset");
  }

  TEST(ScriptFieldEditTest, RejectsAnIntegerOutsideIntsRange)
  {
    constexpr auto max = std::numeric_limits<int>::max();
    constexpr auto min = std::numeric_limits<int>::min();

    EXPECT_EQ(reject(makeField("count", "int", max), "int"), "");
    EXPECT_EQ(reject(makeField("count", "int", min), "int"), "");
    EXPECT_EQ(reject(makeField("count", "int", static_cast<std::int64_t>(max) + 1), "int"),
              "field 'count' value is out of range for int");
    EXPECT_EQ(reject(makeField("count", "int", static_cast<std::int64_t>(min) - 1), "int"),
              "field 'count' value is out of range for int");
  }

  TEST(ScriptFieldEditTest, RejectsAValueOfTheWrongJsonType)
  {
    EXPECT_EQ(reject(makeField("speed", "float", "fast"), "float"), "field 'speed' value does not match type float");
    EXPECT_EQ(reject(makeField("count", "int", 1.5), "int"), "field 'count' value does not match type int");
    EXPECT_EQ(reject(makeField("enabled", "bool", 1), "bool"), "field 'enabled' value does not match type bool");
    EXPECT_EQ(reject(makeField("label", "string", 1.0), "string"), "field 'label' value does not match type string");
    EXPECT_EQ(reject(makeField("offset", "vector3", 1.0), "vector3"), "field 'offset' value does not match type vector3");
  }

  TEST(ScriptFieldEditTest, RejectsAMisshapenVector3)
  {
    EXPECT_EQ(reject(makeField("offset", "vector3", nlohmann::json::array({ 1.0, 2.0 })), "vector3"),
              "field 'offset' value does not match type vector3");
    EXPECT_EQ(reject(makeField("offset", "vector3", nlohmann::json::array({ 1.0, 2.0, 3.0, 4.0 })), "vector3"),
              "field 'offset' value does not match type vector3");
    EXPECT_EQ(reject(makeField("offset", "vector3", nlohmann::json::array({ 1.0, "two", 3.0 })), "vector3"),
              "field 'offset' value does not match type vector3");
  }
}
