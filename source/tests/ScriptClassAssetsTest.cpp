#include <gtest/gtest.h>

#include "ScriptSystemTestFixtures.h"
#include "assets/AssetRegistry.h"
#include "assets/ScriptClassAssets.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {
  using namespace scriptSystemFixtures;

  std::vector<std::string> scriptClassesOf(const AssetRegistry& registry)
  {
    std::vector<std::string> names;
    for (const auto& [uuid, record] : registry.getAssets())
    {
      if (record.type == AssetType::Script)
      {
        names.push_back(record.className);
      }
    }

    std::sort(names.begin(), names.end());

    return names;
  }

  TEST(ScriptClassAssets, RegistersAScriptRecordForEveryCompiledClassTheProjectDoesNotList)
  {
    AssetRegistry registry;

    const auto added = registerScriptClassAssets(registry, { "Spinner", "Collectible" });

    EXPECT_EQ(added, 2u);
    EXPECT_EQ(scriptClassesOf(registry), (std::vector<std::string>{ "Collectible", "Spinner" }));
  }

  TEST(ScriptClassAssets, DerivesTheSameUuidForAClassOnEveryMachine)
  {
    AssetRegistry first;
    AssetRegistry second;

    registerScriptClassAssets(first, { "Spinner" });
    registerScriptClassAssets(second, { "Spinner" });

    EXPECT_EQ(uuids::to_string(scriptClassUuid("Spinner")), "a3ea623f-88b5-59dc-a2c2-f6b95121c510");
    EXPECT_NE(scriptClassUuid("Spinner"), scriptClassUuid("Collectible"));
    ASSERT_NE(first.getByUUID(scriptClassUuid("Spinner")), nullptr);
    EXPECT_NE(second.getByUUID(scriptClassUuid("Spinner")), nullptr);
  }

  TEST(ScriptClassAssets, KeepsTheRecordTheProjectAlreadyHoldsForAClass)
  {
    AssetRegistry registry;
    const auto listed = uuids::uuid::from_string("11111111-1111-1111-1111-111111111111").value();
    registry.registerAsset({ .uuid = listed, .type = AssetType::Script,
      .path = "scripts/UserScripts/PlayerScript.cs", .className = "PlayerScript" });

    const auto added = registerScriptClassAssets(registry, { "PlayerScript", "Spinner" });

    EXPECT_EQ(added, 1u);
    EXPECT_EQ(scriptClassesOf(registry), (std::vector<std::string>{ "PlayerScript", "Spinner" }));
    const auto* kept = registry.getByUUID(listed);
    ASSERT_NE(kept, nullptr);
    EXPECT_EQ(kept->path, "scripts/UserScripts/PlayerScript.cs");
    EXPECT_EQ(registry.getByUUID(scriptClassUuid("PlayerScript")), nullptr);
  }

  TEST(ScriptClassAssets, AddsNothingOnASecondPassOrForAnEmptyName)
  {
    AssetRegistry registry;
    registerScriptClassAssets(registry, { "Spinner" });
    const auto version = registry.getVersion();

    EXPECT_EQ(registerScriptClassAssets(registry, { "Spinner", "" }), 0u);
    EXPECT_EQ(registry.getVersion(), version);
    EXPECT_EQ(registerScriptClassAssets(registry, { "Spawner" }), 1u);
    EXPECT_GT(registry.getVersion(), version);
  }

  TEST(ScriptClassAssets, ARecordRoundTripsThroughTheSnapshotWithItsClassName)
  {
    AssetRegistry registry;
    registerScriptClassAssets(registry, { "Spinner" });

    net::Message message(net::MessageType::snapshot);
    registry.pack(message);
    AssetRegistry received;
    net::MessageReader reader(message);
    received.unpack(reader);

    const auto* record = received.getByUUID(scriptClassUuid("Spinner"));
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->className, "Spinner");
    EXPECT_EQ(record->type, AssetType::Script);
  }

  TEST_F(ScriptSystemTest, ReportsTheRuntimesClassNamesOnceTheRuntimeExists)
  {
    state.classNames = { "Spinner", "Spawner" };

    EXPECT_TRUE(scriptSystem.scriptClassNames().empty());

    scriptSystem.warmUp();

    EXPECT_EQ(scriptSystem.scriptClassNames(), (std::vector<std::string>{ "Spinner", "Spawner" }));
  }
}
