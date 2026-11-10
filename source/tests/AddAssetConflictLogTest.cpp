#include <gtest/gtest.h>

#include "Log.h"
#include "RingBufferSink.h"
#include "TestScene.h"
#include "Replication.h"
#include "assets/AssetRegistry.h"
#include "scenes/SceneManager.h"

#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <uuid.h>

namespace {
  const auto sceneUUID = uuids::uuid::from_string("22222222-2222-2222-2222-222222222222").value();
  const auto prefabUUID = uuids::uuid::from_string("33333333-3333-3333-3333-333333333333").value();

  class AddAssetConflictLogTest : public testing::Test {
  protected:
    void SetUp() override
    {
      Log::setMinimumLevel(LogLevel::trace);
      m_sink = std::make_shared<RingBufferSink>();
      Log::addSink(m_sink);
    }

    void TearDown() override
    {
      Log::removeSink(m_sink);
      Log::setMinimumLevel(LogLevel::info);
    }

    std::shared_ptr<RingBufferSink> m_sink;
  };
}

TEST_F(AddAssetConflictLogTest, WarnsWhenAPrefabIsAddedUnderASceneName)
{
  const fixtures::Scene scene;
  AssetRegistry registry;
  SceneManager sceneManager;

  ASSERT_TRUE(replication::applyAddAsset(registry, sceneManager, scene.componentRegistry,
                                         { { "assetType", "scene" }, { "uuid", uuids::to_string(sceneUUID) },
                                           { "name", "Main" } }));
  ASSERT_TRUE(m_sink->snapshot().empty());

  EXPECT_FALSE(replication::applyAddAsset(registry, sceneManager, scene.componentRegistry,
                                          { { "assetType", "prefab" }, { "uuid", uuids::to_string(prefabUUID) },
                                            { "name", "Main" }, { "body", "{}" } }));

  const auto entries = m_sink->snapshot();
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].level, LogLevel::warn);
  EXPECT_EQ(entries[0].category, LogCategory::assets);
  EXPECT_NE(entries[0].message.find("Main"), std::string::npos);
  EXPECT_NE(entries[0].message.find("prefab"), std::string::npos);
  EXPECT_NE(entries[0].message.find("scene"), std::string::npos);
}

TEST_F(AddAssetConflictLogTest, StaysQuietForAPrefabUnderAFreshName)
{
  const fixtures::Scene scene;
  AssetRegistry registry;
  SceneManager sceneManager;

  EXPECT_TRUE(replication::applyAddAsset(registry, sceneManager, scene.componentRegistry,
                                         { { "assetType", "prefab" }, { "uuid", uuids::to_string(prefabUUID) },
                                           { "name", "Fresh" }, { "body", "{}" } }));
  EXPECT_TRUE(m_sink->snapshot().empty());
}
