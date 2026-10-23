#include <components/ScriptFieldAsset.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <uuid.h>

namespace {
  using nlohmann::json;

  json assetField(const json& type, const json& asset)
  {
    return { { "name", "m_a" }, { "type", type }, { "asset", asset } };
  }

  TEST(ScriptFieldAsset, AStringFieldMapsItsAssetKindToTheAssetType)
  {
    EXPECT_EQ(scriptFieldAssetType(assetField("string", "prefab")), AssetType::Prefab);
    EXPECT_EQ(scriptFieldAssetType(assetField("string", "model")), AssetType::Model);
    EXPECT_EQ(scriptFieldAssetType(assetField("string", "texture")), AssetType::Texture);
  }

  TEST(ScriptFieldAsset, AFieldWithoutAUsableAssetKindHasNoType)
  {
    EXPECT_FALSE(scriptFieldAssetType(assetField("string", "scene")));
    EXPECT_FALSE(scriptFieldAssetType(assetField("string", "script")));
    EXPECT_FALSE(scriptFieldAssetType(assetField("string", "unknown")));
    EXPECT_FALSE(scriptFieldAssetType(assetField("string", 3)));
    EXPECT_FALSE(scriptFieldAssetType(assetField("float", "texture")));
    EXPECT_FALSE(scriptFieldAssetType(assetField(3, "texture")));
    EXPECT_FALSE(scriptFieldAssetType({ { "name", "m_a" }, { "type", "string" } }));
    EXPECT_FALSE(scriptFieldAssetType({ { "name", "m_a" }, { "asset", "model" } }));
    EXPECT_FALSE(scriptFieldAssetType(json::array()));
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
