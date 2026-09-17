#include <gtest/gtest.h>

#include "ScriptFieldEdit.h"

#include <nlohmann/json.hpp>
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
    EXPECT_EQ(reject(makeField("label", "string", "hello"), "string"),
              "field 'label' has unsupported type string");
  }

  TEST(ScriptFieldEditTest, RejectsAValueOfTheWrongJsonType)
  {
    EXPECT_EQ(reject(makeField("speed", "float", "fast"), "float"), "field 'speed' value is not a float");
    EXPECT_EQ(reject(makeField("count", "int", 1.5), "int"), "field 'count' value is not a int");
    EXPECT_EQ(reject(makeField("enabled", "bool", 1), "bool"), "field 'enabled' value is not a bool");
    EXPECT_EQ(reject(makeField("offset", "vector3", 1.0), "vector3"), "field 'offset' value is not a vector3");
  }

  TEST(ScriptFieldEditTest, RejectsAMisshapenVector3)
  {
    EXPECT_EQ(reject(makeField("offset", "vector3", nlohmann::json::array({ 1.0, 2.0 })), "vector3"),
              "field 'offset' value is not a vector3");
    EXPECT_EQ(reject(makeField("offset", "vector3", nlohmann::json::array({ 1.0, 2.0, 3.0, 4.0 })), "vector3"),
              "field 'offset' value is not a vector3");
    EXPECT_EQ(reject(makeField("offset", "vector3", nlohmann::json::array({ 1.0, "two", 3.0 })), "vector3"),
              "field 'offset' value is not a vector3");
  }
}
