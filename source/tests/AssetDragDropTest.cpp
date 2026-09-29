#include <gtest/gtest.h>

#include <AssetDragDrop.h>
#include <assets/AssetRegistry.h>

#include <string>

namespace {
  uuids::uuid uuidFrom(const std::string& text)
  {
    return uuids::uuid::from_string(text).value();
  }

  const auto sceneUUID = uuidFrom("11111111-1111-1111-1111-111111111111");
  const auto modelUUID = uuidFrom("22222222-2222-2222-2222-222222222222");
  const auto missingUUID = uuidFrom("99999999-9999-9999-9999-999999999999");

  class AssetDragDropTest : public testing::Test
  {
  protected:
    AssetDragDropTest()
    {
      m_registry.registerAsset({ .uuid = sceneUUID, .type = AssetType::Scene, .path = "Main" });
      m_registry.registerAsset({ .uuid = modelUUID, .type = AssetType::Model, .path = "assets/models/cube.obj" });
    }

    [[nodiscard]] std::optional<uuids::uuid> resolve(const std::string& payload) const
    {
      return assetDragDrop::sceneFromPayload(payload.data(), static_cast<int>(payload.size()), m_registry);
    }

    AssetRegistry m_registry;
  };
}

TEST_F(AssetDragDropTest, RegisteredSceneResolvesToItsUUID)
{
  const auto resolved = resolve(uuids::to_string(sceneUUID));

  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, sceneUUID);
}

TEST_F(AssetDragDropTest, NonSceneAssetIsRejectedWhileSceneResolves)
{
  EXPECT_FALSE(resolve(uuids::to_string(modelUUID)).has_value());
  EXPECT_TRUE(resolve(uuids::to_string(sceneUUID)).has_value());
}

TEST_F(AssetDragDropTest, MalformedOrUnregisteredPayloadIsRejectedWhileSceneResolves)
{
  EXPECT_FALSE(resolve("not-a-uuid").has_value());
  EXPECT_FALSE(resolve("").has_value());
  EXPECT_FALSE(resolve(uuids::to_string(missingUUID)).has_value());
  EXPECT_FALSE(assetDragDrop::sceneFromPayload(nullptr, 36, m_registry).has_value());
  EXPECT_TRUE(resolve(uuids::to_string(sceneUUID)).has_value());
}

TEST_F(AssetDragDropTest, PayloadIsReadOnlyToItsDeclaredSize)
{
  const std::string buffer = uuids::to_string(sceneUUID) + uuids::to_string(modelUUID);

  const auto resolved = assetDragDrop::sceneFromPayload(buffer.data(), 36, m_registry);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, sceneUUID);

  EXPECT_FALSE(assetDragDrop::sceneFromPayload(buffer.data(), 72, m_registry).has_value());
}

TEST(AssetDragDropPayloadIdTest,SceneIdIsDistinctFromEveryOtherType)
{
  EXPECT_STREQ(assetDragDrop::payloadId(AssetType::Scene), assetDragDrop::scene);

  const AssetType others[] = { AssetType::Model, AssetType::Texture, AssetType::Script, AssetType::Prefab };
  for (const AssetType type : others)
  {
    const char* id = assetDragDrop::payloadId(type);
    ASSERT_NE(id, nullptr);
    EXPECT_STRNE(id, assetDragDrop::scene);
  }

  EXPECT_EQ(assetDragDrop::payloadId(AssetType::Unknown), nullptr);
}
