#include <gtest/gtest.h>

#include "ScriptSystemTestFixtures.h"
#include "assets/AssetRegistry.h"
#include "ComponentRegistry.h"
#include "SceneScriptsPanelLogic.h"
#include "assets/ScriptClassAssets.h"
#include "edits/RecordEdits.h"
#include "scenes/SceneAsset.h"

#include <Protocol.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {
  using namespace scriptSystemFixtures;

  using Compiled = std::optional<std::vector<ScriptClassInfo>>;

  Compiled objectClasses(const std::vector<std::string>& names)
  {
    std::vector<ScriptClassInfo> classes;
    for (const auto& name : names)
    {
      classes.push_back({ name, false });
    }

    return classes;
  }

  uuids::uuid fixedUuid(const char* text)
  {
    return uuids::uuid::from_string(text).value();
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

    EXPECT_TRUE(syncScriptClassAssets(registry, objectClasses({ "Spinner", "Collectible" })));
    EXPECT_EQ(scriptClassesOf(registry), (std::vector<std::string>{ "Collectible", "Spinner" }));
  }

  TEST(ScriptClassAssets, DerivesTheSameUuidForAClassOnEveryMachine)
  {
    AssetRegistry first;
    AssetRegistry second;

    syncScriptClassAssets(first, objectClasses({ "Spinner" }));
    syncScriptClassAssets(second, objectClasses({ "Spinner" }));

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

    EXPECT_TRUE(syncScriptClassAssets(registry, objectClasses({ "PlayerScript", "Spinner" })));
    EXPECT_EQ(scriptClassesOf(registry), (std::vector<std::string>{ "PlayerScript", "Spinner" }));
    const auto* kept = registry.getByUUID(listed);
    ASSERT_NE(kept, nullptr);
    EXPECT_EQ(kept->path, "scripts/UserScripts/PlayerScript.cs");
    EXPECT_EQ(registry.getByUUID(scriptClassUuid("PlayerScript")), nullptr);
  }

  TEST(ScriptClassAssets, AddsNothingOnASecondPassOrForAnEmptyName)
  {
    AssetRegistry registry;
    syncScriptClassAssets(registry, objectClasses({ "Spinner" }));
    const auto version = registry.getVersion();

    EXPECT_FALSE(syncScriptClassAssets(registry, objectClasses({ "Spinner", "" })));
    EXPECT_EQ(registry.getVersion(), version);
    EXPECT_TRUE(syncScriptClassAssets(registry, objectClasses({ "Spinner", "Spawner" })));
    EXPECT_GT(registry.getVersion(), version);
  }

  TEST(ScriptClassAssets, ARecordRoundTripsThroughTheSnapshotWithItsClassName)
  {
    AssetRegistry registry;
    syncScriptClassAssets(registry, objectClasses({ "Spinner" }));

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
    syncScriptClassAssets(registry, objectClasses({ "Spinner" }));
    const auto fileUuid = uuids::uuid::from_string("22222222-2222-2222-2222-222222222222").value();

    registry.registerAsset({ .uuid = fileUuid, .type = AssetType::Script,
      .path = "scripts/Spinner.cs", .className = "Spinner" });

    EXPECT_EQ(scriptClassesOf(registry), (std::vector<std::string>{ "Spinner" }));
    EXPECT_EQ(registry.getByUUID(fileUuid), nullptr);
    const auto* merged = registry.getByUUID(scriptClassUuid("Spinner"));
    ASSERT_NE(merged, nullptr);
    EXPECT_EQ(merged->path, "scripts/Spinner.cs");
    EXPECT_EQ(registry.getByPath("scripts/Spinner.cs"), merged);
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

  TEST(ScriptClassAssets, OnlyARecordOfAClassThatNoLongerCompilesCanBeDeleted)
  {
    AssetRecord compiled{ .uuid = scriptClassUuid("Spinner"), .type = AssetType::Script, .className = "Spinner",
                          .scriptKind = ScriptKind::Object };
    AssetRecord orphan{ .uuid = scriptClassUuid("Gone"), .type = AssetType::Script, .path = "g.cs",
                        .className = "Gone" };

    EXPECT_FALSE(canDeleteAsset(compiled));
    compiled.path = "s.cs";
    EXPECT_FALSE(canDeleteAsset(compiled));
    EXPECT_TRUE(canDeleteAsset(orphan));
    EXPECT_TRUE(canDeleteAsset({ .uuid = scriptClassUuid("M"), .type = AssetType::Model }));
  }

  TEST(ScriptClassAssets, ARegistrySavesOnlyFileBackedScriptRecords)
  {
    AssetRegistry registry;
    registry.registerAsset({ .uuid = fixedUuid("11111111-1111-1111-1111-111111111111"), .type = AssetType::Script,
                             .path = "scripts/Mover.cs", .className = "Mover" });
    syncScriptClassAssets(registry, objectClasses({ "Mover", "Spinner" }));
    ASSERT_NE(registry.getByUUID(scriptClassUuid("Spinner")), nullptr);

    const auto saved = registry.serialize();
    AssetRegistry loaded;
    loaded.loadFromJSON(saved);

    EXPECT_EQ(scriptClassesOf(loaded), (std::vector<std::string>{ "Mover" }));
    EXPECT_EQ(saved.at("scripts").size(), 1u);
  }

  TEST(ScriptClassAssets, EveryRecordOfACompiledClassTakesTheCompileKind)
  {
    AssetRegistry registry;
    const auto listed = fixedUuid("11111111-1111-1111-1111-111111111111");
    registry.registerAsset({ .uuid = listed, .type = AssetType::Script, .path = "scripts/Rules.cs",
                             .className = "Rules" });

    EXPECT_TRUE(syncScriptClassAssets(registry, Compiled(std::vector<ScriptClassInfo>{ { "Rules", true } })));

    EXPECT_EQ(registry.getByUUID(listed)->scriptKind, ScriptKind::Scene);
    EXPECT_FALSE(usableOnObject(*registry.getByUUID(listed)));
  }

  TEST(ScriptClassAssets, ARecordWhoseClassStopsCompilingLosesItsKindOrItself)
  {
    AssetRegistry registry;
    const auto listed = fixedUuid("11111111-1111-1111-1111-111111111111");
    registry.registerAsset({ .uuid = listed, .type = AssetType::Script, .path = "scripts/Mover.cs",
                             .className = "Mover" });
    syncScriptClassAssets(registry, objectClasses({ "Mover", "Spinner" }));
    ASSERT_EQ(registry.getByUUID(listed)->scriptKind, ScriptKind::Object);
    ASSERT_NE(registry.getByUUID(scriptClassUuid("Spinner")), nullptr);

    EXPECT_TRUE(syncScriptClassAssets(registry, objectClasses({})));

    ASSERT_NE(registry.getByUUID(listed), nullptr);
    EXPECT_EQ(registry.getByUUID(listed)->scriptKind, ScriptKind::Either);
    EXPECT_EQ(registry.getByUUID(scriptClassUuid("Spinner")), nullptr);
    EXPECT_FALSE(syncScriptClassAssets(registry, objectClasses({})));
  }

  TEST(ScriptClassAssets, NothingChangesBeforeACompileHasSucceeded)
  {
    AssetRegistry registry;
    syncScriptClassAssets(registry, objectClasses({ "Spinner" }));
    const auto version = registry.getVersion();

    EXPECT_FALSE(syncScriptClassAssets(registry, std::nullopt));

    EXPECT_EQ(registry.getVersion(), version);
    EXPECT_EQ(scriptClassesOf(registry), (std::vector<std::string>{ "Spinner" }));
    EXPECT_TRUE(syncScriptClassAssets(registry, objectClasses({})));
    EXPECT_TRUE(scriptClassesOf(registry).empty());
  }

  TEST(ScriptClassAssets, TheKindDecidesWhichHostAClassCanAttachTo)
  {
    AssetRegistry registry;
    syncScriptClassAssets(registry, Compiled(std::vector<ScriptClassInfo>{ { "Spinner", false }, { "Rules", true } }));
    const auto* object = registry.getByUUID(scriptClassUuid("Spinner"));
    const auto* scene = registry.getByUUID(scriptClassUuid("Rules"));
    ASSERT_NE(object, nullptr);
    ASSERT_NE(scene, nullptr);

    EXPECT_TRUE(usableOnObject(*object));
    EXPECT_FALSE(usableOnScene(*object));
    EXPECT_TRUE(usableOnScene(*scene));
    EXPECT_FALSE(usableOnObject(*scene));

    EXPECT_EQ(object->scriptKind, ScriptKind::Object);
    EXPECT_EQ(scene->scriptKind, ScriptKind::Scene);

    const AssetRecord listed{ .uuid = scriptClassUuid("Other"), .type = AssetType::Script, .path = "o.cs",
                              .className = "Other" };
    EXPECT_TRUE(usableOnObject(listed));
    EXPECT_TRUE(usableOnScene(listed));
  }

  TEST(ScriptClassAssets, TheKindSurvivesTheSnapshot)
  {
    AssetRegistry registry;
    syncScriptClassAssets(registry, Compiled(std::vector<ScriptClassInfo>{ { "Rules", true } }));

    net::Message message(net::MessageType::snapshot);
    registry.pack(message);
    AssetRegistry received;
    net::MessageReader reader(message);
    received.unpack(reader);

    const auto* record = received.getByUUID(scriptClassUuid("Rules"));
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->scriptKind, ScriptKind::Scene);
    EXPECT_FALSE(usableOnObject(*record));
    EXPECT_TRUE(usableOnScene(*record));
  }

  TEST(ScriptClassAssets, ASceneScriptClassIsNotOfferedToTheObjectPickerOrAnObjectClassToTheScenePanel)
  {
    AssetRegistry registry;
    syncScriptClassAssets(registry, Compiled(std::vector<ScriptClassInfo>{ { "Spinner", false }, { "Rules", true } }));
    const auto scene = std::make_shared<SceneAsset>(scriptClassUuid("Scene"), "S",
                                                    std::make_shared<ComponentRegistry>());

    EXPECT_EQ(sceneScriptPanel::availableClasses(registry, scene->getScripts()),
              (std::vector<std::string>{ "Rules" }));
  }

  TEST(RecordEdits, ASecondScriptRecordForAKnownClassRecordsNothingButANewClassRecordsAnAdd)
  {
    AssetRegistry registry;
    syncScriptClassAssets(registry, objectClasses({ "Spinner" }));
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
    state.classNames = std::vector<ScriptClassInfo>{ { "Spinner", false }, { "Spawner", true } };

    EXPECT_FALSE(scriptSystem.scriptClassNames().has_value());

    scriptSystem.warmUp();

    const auto reported = scriptSystem.scriptClassNames();
    ASSERT_TRUE(reported.has_value());
    ASSERT_EQ(reported->size(), 2u);
    EXPECT_EQ((*reported)[0].name, "Spinner");
    EXPECT_FALSE((*reported)[0].sceneScript);
    EXPECT_TRUE((*reported)[1].sceneScript);
  }

  TEST_F(ScriptSystemTest, ReportsNothingUntilTheRuntimeHasCompiledEvenWithAnEngine)
  {
    scriptSystem.warmUp();

    EXPECT_FALSE(scriptSystem.scriptClassNames().has_value());

    state.classNames = std::vector<ScriptClassInfo>{};

    ASSERT_TRUE(scriptSystem.scriptClassNames().has_value());
    EXPECT_TRUE(scriptSystem.scriptClassNames()->empty());
  }
}
