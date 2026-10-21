#include <gtest/gtest.h>

#include "ScriptSystemTestFixtures.h"
#include "assets/AssetRegistry.h"
#include "ComponentRegistry.h"
#include "SceneScriptsPanelLogic.h"
#include "assets/ScriptClassAssets.h"
#include "edits/RecordEdits.h"
#include "scenes/SceneAsset.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {
  using namespace scriptSystemFixtures;

  std::vector<ScriptClassInfo> objectClasses(const std::vector<std::string>& names)
  {
    std::vector<ScriptClassInfo> classes;
    for (const auto& name : names)
    {
      classes.push_back({ name, false });
    }

    return classes;
  }

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

    const auto added = registerScriptClassAssets(registry, objectClasses({ "Spinner", "Collectible" }));

    EXPECT_EQ(added, 2u);
    EXPECT_EQ(scriptClassesOf(registry), (std::vector<std::string>{ "Collectible", "Spinner" }));
  }

  TEST(ScriptClassAssets, DerivesTheSameUuidForAClassOnEveryMachine)
  {
    AssetRegistry first;
    AssetRegistry second;

    registerScriptClassAssets(first, objectClasses({ "Spinner" }));
    registerScriptClassAssets(second, objectClasses({ "Spinner" }));

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

    const auto added = registerScriptClassAssets(registry, objectClasses({ "PlayerScript", "Spinner" }));

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
    registerScriptClassAssets(registry, objectClasses({ "Spinner" }));
    const auto version = registry.getVersion();

    EXPECT_EQ(registerScriptClassAssets(registry, objectClasses({ "Spinner", "" })), 0u);
    EXPECT_EQ(registry.getVersion(), version);
    EXPECT_EQ(registerScriptClassAssets(registry, objectClasses({ "Spawner" })), 1u);
    EXPECT_GT(registry.getVersion(), version);
  }

  TEST(ScriptClassAssets, ARecordRoundTripsThroughTheSnapshotWithItsClassName)
  {
    AssetRegistry registry;
    registerScriptClassAssets(registry, objectClasses({ "Spinner" }));

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

  TEST(ScriptClassAssets, ALateFileBackedRecordMergesIntoTheCompiledOneAndFillsItsPath)
  {
    AssetRegistry registry;
    registerScriptClassAssets(registry, objectClasses({ "Spinner" }));
    const auto fileUuid = uuids::uuid::from_string("22222222-2222-2222-2222-222222222222").value();

    registry.registerAsset({ .uuid = fileUuid, .type = AssetType::Script,
      .path = "scripts/Spinner.cs", .className = "Spinner" });

    EXPECT_EQ(scriptClassesOf(registry), (std::vector<std::string>{ "Spinner" }));
    EXPECT_EQ(registry.getByUUID(fileUuid), nullptr);
    const auto* merged = registry.getByUUID(scriptClassUuid("Spinner"));
    ASSERT_NE(merged, nullptr);
    EXPECT_EQ(merged->path, "scripts/Spinner.cs");
    EXPECT_EQ(registry.getByPath("scripts/Spinner.cs"), merged);
    EXPECT_FALSE(isCompiledScriptRecord(*merged));
  }

  TEST(ScriptClassAssets, AFileBackedRecordKeepsItsPathWhenALaterOneNamesTheSameClass)
  {
    AssetRegistry registry;
    const auto first = uuids::uuid::from_string("11111111-1111-1111-1111-111111111111").value();
    const auto second = uuids::uuid::from_string("22222222-2222-2222-2222-222222222222").value();
    registry.registerAsset({ .uuid = first, .type = AssetType::Script, .path = "a.cs", .className = "Dup" });

    registry.registerAsset({ .uuid = second, .type = AssetType::Script, .path = "b.cs", .className = "Dup" });

    ASSERT_NE(registry.getByUUID(first), nullptr);
    EXPECT_EQ(registry.getByUUID(first)->path, "a.cs");
    EXPECT_EQ(registry.getByUUID(second), nullptr);
    EXPECT_EQ(scriptClassesOf(registry), (std::vector<std::string>{ "Dup" }));
  }

  TEST(ScriptClassAssets, OnlyAPathlessScriptRecordIsCompiled)
  {
    const auto uuid = scriptClassUuid("Spinner");

    EXPECT_TRUE(isCompiledScriptRecord({ .uuid = uuid, .type = AssetType::Script, .className = "Spinner" }));
    EXPECT_FALSE(isCompiledScriptRecord({ .uuid = uuid, .type = AssetType::Script, .path = "s.cs",
                                          .className = "Spinner" }));
    EXPECT_FALSE(isCompiledScriptRecord({ .uuid = uuid, .type = AssetType::Model }));
  }

  TEST(ScriptClassAssets, TheKindDecidesWhichHostAClassCanAttachTo)
  {
    AssetRegistry registry;
    registerScriptClassAssets(registry, { { "Spinner", false }, { "Rules", true } });
    const auto* object = registry.getByUUID(scriptClassUuid("Spinner"));
    const auto* scene = registry.getByUUID(scriptClassUuid("Rules"));
    ASSERT_NE(object, nullptr);
    ASSERT_NE(scene, nullptr);

    EXPECT_TRUE(usableOnObject(*object));
    EXPECT_FALSE(usableOnScene(*object));
    EXPECT_TRUE(usableOnScene(*scene));
    EXPECT_FALSE(usableOnObject(*scene));

    const AssetRecord listed{ .uuid = scriptClassUuid("Other"), .type = AssetType::Script, .path = "o.cs",
                              .className = "Other" };
    EXPECT_TRUE(usableOnObject(listed));
    EXPECT_TRUE(usableOnScene(listed));
  }

  TEST(ScriptClassAssets, TheKindSurvivesTheSnapshot)
  {
    AssetRegistry registry;
    registerScriptClassAssets(registry, { { "Rules", true } });

    net::Message message(net::MessageType::snapshot);
    registry.pack(message);
    AssetRegistry received;
    net::MessageReader reader(message);
    received.unpack(reader);

    const auto* record = received.getByUUID(scriptClassUuid("Rules"));
    ASSERT_NE(record, nullptr);
    EXPECT_FALSE(usableOnObject(*record));
  }

  TEST(ScriptClassAssets, ASceneScriptClassIsNotOfferedToTheObjectPickerOrAnObjectClassToTheScenePanel)
  {
    AssetRegistry registry;
    registerScriptClassAssets(registry, { { "Spinner", false }, { "Rules", true } });
    const auto scene = std::make_shared<SceneAsset>(scriptClassUuid("Scene"), "S",
                                                    std::make_shared<ComponentRegistry>());

    EXPECT_EQ(sceneScriptPanel::availableClasses(registry, scene->getScripts()),
              (std::vector<std::string>{ "Rules" }));
  }

  TEST(RecordEdits, ASecondScriptRecordForAKnownClassRecordsNothingButANewClassRecordsAnAdd)
  {
    AssetRegistry registry;
    registerScriptClassAssets(registry, objectClasses({ "Spinner" }));
    const auto fresh = uuids::uuid::from_string("33333333-3333-3333-3333-333333333333").value();

    const nlohmann::json known = { { "assetType", "script" }, { "uuid", uuids::to_string(fresh) },
                                   { "path", "scripts/Spinner.cs" }, { "className", "Spinner" } };
    const nlohmann::json unknown = { { "assetType", "script" }, { "uuid", uuids::to_string(fresh) },
                                     { "path", "scripts/Mover.cs" }, { "className", "Mover" } };

    EXPECT_FALSE(edits::commandForAddAsset(known, registry).has_value());
    EXPECT_TRUE(edits::commandForAddAsset(unknown, registry).has_value());
  }

  TEST_F(ScriptSystemTest, ReportsTheRuntimesClassNamesOnceTheRuntimeExists)
  {
    state.classNames = { { "Spinner", false }, { "Spawner", true } };

    EXPECT_TRUE(scriptSystem.scriptClassNames().empty());

    scriptSystem.warmUp();

    const auto reported = scriptSystem.scriptClassNames();
    ASSERT_EQ(reported.size(), 2u);
    EXPECT_EQ(reported[0].name, "Spinner");
    EXPECT_FALSE(reported[0].sceneScript);
    EXPECT_TRUE(reported[1].sceneScript);
  }
}
