#include <components/ScriptFieldAsset.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <uuid.h>

namespace {
  using nlohmann::json;

  TEST(ScriptFieldAsset, APrefabStringFieldIsAPrefabReference)
  {
    EXPECT_TRUE(isPrefabReferenceField({ { "name", "m_prefab" }, { "type", "string" }, { "asset", "prefab" } }));
  }

  TEST(ScriptFieldAsset, AFieldWithoutAPrefabAssetIsNotAReference)
  {
    EXPECT_FALSE(isPrefabReferenceField({ { "name", "m_a" }, { "type", "string" } }));
    EXPECT_FALSE(isPrefabReferenceField({ { "name", "m_a" }, { "type", "string" }, { "asset", "model" } }));
    EXPECT_FALSE(isPrefabReferenceField({ { "name", "m_a" }, { "type", "string" }, { "asset", 3 } }));
    EXPECT_FALSE(isPrefabReferenceField({ { "name", "m_a" }, { "type", "float" }, { "asset", "prefab" } }));
    EXPECT_FALSE(isPrefabReferenceField({ { "name", "m_a" }, { "asset", "prefab" } }));
    EXPECT_FALSE(isPrefabReferenceField(json::array()));
  }

  TEST(ScriptFieldAsset, AValidUuidStringParses)
  {
    const auto text = "6d2b4f19-08ac-4d73-9e52-b83a1c6f0e47";
    const auto parsed = scriptFieldUUID(json(text));

    EXPECT_FALSE(parsed.is_nil());
    EXPECT_EQ(uuids::to_string(parsed), text);
  }

  TEST(ScriptFieldAsset, AnEmptyGarbageOrNonStringValueIsNil)
  {
    EXPECT_TRUE(scriptFieldUUID(json("")).is_nil());
    EXPECT_TRUE(scriptFieldUUID(json("not a uuid")).is_nil());
    EXPECT_TRUE(scriptFieldUUID(json(7)).is_nil());
    EXPECT_TRUE(scriptFieldUUID(json()).is_nil());
  }
}
