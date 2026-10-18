#include <gtest/gtest.h>

#include "TestScene.h"

#include <AssetNaming.h>
#include <assets/AssetRegistry.h>
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Transform.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <nlohmann/json.hpp>
#include <string>

namespace {
  uuids::uuid makeUUID(const unsigned n)
  {
    char text[40];
    std::snprintf(text, sizeof(text), "%08x-0000-0000-0000-000000000000", n);
    return uuids::uuid::from_string(text).value();
  }

  class AssetNamingTest : public testing::Test
  {
  protected:
    void addScene(const std::string& name, const std::string& displayName = "")
    {
      m_registry.registerAsset({ .uuid = makeUUID(++m_next), .type = AssetType::Scene, .path = name,
                                 .displayName = displayName });
    }

    void addPrefab(const std::string& name)
    {
      m_registry.registerAsset({ .uuid = makeUUID(++m_next), .type = AssetType::Prefab, .path = name });
    }

    void addScript(const std::string& className)
    {
      m_registry.registerAsset({ .uuid = makeUUID(++m_next), .type = AssetType::Script,
                                 .path = "scripts/UserScripts/" + className + ".cs", .className = className });
    }

    AssetRegistry m_registry;
    unsigned m_next = 0;
  };

  const auto noFiles = [](const std::string&) { return false; };
}

TEST_F(AssetNamingTest, FreeSceneNameIsReturnedAsIs)
{
  addScene("Other");
  EXPECT_EQ(assetNaming::uniqueSceneName(m_registry, "New Scene"), "New Scene");
}

TEST_F(AssetNamingTest, TakenSceneNameGetsNumberedSuffixSkippingGaps)
{
  addScene("New Scene");
  EXPECT_EQ(assetNaming::uniqueSceneName(m_registry, "New Scene"), "New Scene (2)");

  addScene("New Scene (2)");
  addScene("New Scene (4)");
  EXPECT_EQ(assetNaming::uniqueSceneName(m_registry, "New Scene"), "New Scene (3)");

  addScene("New Scene (3)");
  EXPECT_EQ(assetNaming::uniqueSceneName(m_registry, "New Scene"), "New Scene (5)");
}

TEST_F(AssetNamingTest, TakenSuffixedSceneNameGrowsTheNumber)
{
  addScene("Level");
  addScene("Level (2)");
  EXPECT_EQ(assetNaming::uniqueSceneName(m_registry, "Level (2)"), "Level (3)");
}

TEST_F(AssetNamingTest, DisplayNameOverrideCountsAsTaken)
{
  addScene("Main", "New Scene");
  EXPECT_TRUE(assetNaming::isNameTaken(m_registry, AssetType::Scene, "New Scene"));
  EXPECT_FALSE(assetNaming::isNameTaken(m_registry, AssetType::Scene, "Unused"));
  EXPECT_EQ(assetNaming::uniqueSceneName(m_registry, "New Scene"), "New Scene (2)");
}

TEST_F(AssetNamingTest, IsNameTakenIsScopedToTheType)
{
  addScene("Shared");
  EXPECT_TRUE(assetNaming::isNameTaken(m_registry, AssetType::Scene, "Shared"));
  EXPECT_FALSE(assetNaming::isNameTaken(m_registry, AssetType::Script, "Shared"));

  addScript("OnlyScript");
  EXPECT_TRUE(assetNaming::isNameTaken(m_registry, AssetType::Script, "OnlyScript"));
  EXPECT_FALSE(assetNaming::isNameTaken(m_registry, AssetType::Scene, "OnlyScript"));
}

TEST_F(AssetNamingTest, IsNameTakenIsCaseSensitive)
{
  addScene("Main");
  EXPECT_TRUE(assetNaming::isNameTaken(m_registry, AssetType::Scene, "Main"));
  EXPECT_FALSE(assetNaming::isNameTaken(m_registry, AssetType::Scene, "main"));
}

TEST_F(AssetNamingTest, FreeScriptNameIsReturnedAsIs)
{
  addScript("Other");
  EXPECT_EQ(assetNaming::uniqueScriptName(m_registry, "NewScript", noFiles), "NewScript");
}

TEST_F(AssetNamingTest, ScriptNameSkipsRegisteredClassNamesWithoutASpace)
{
  addScript("NewScript");
  addScript("NewScript2");

  const auto name = assetNaming::uniqueScriptName(m_registry, "NewScript", noFiles);
  EXPECT_EQ(name, "NewScript3");
  EXPECT_EQ(name.find(' '), std::string::npos);
  EXPECT_EQ(name.find('('), std::string::npos);
}

TEST_F(AssetNamingTest, ScriptNameSkipsAFileTheLambdaReports)
{
  const auto fileExists = [](const std::string& name) { return name == "NewScript" || name == "NewScript2"; };

  EXPECT_EQ(assetNaming::uniqueScriptName(m_registry, "NewScript", noFiles), "NewScript");
  EXPECT_EQ(assetNaming::uniqueScriptName(m_registry, "NewScript", fileExists), "NewScript3");
}

TEST_F(AssetNamingTest, FreePrefabNameIsReturnedAsIs)
{
  addPrefab("Other");
  EXPECT_EQ(assetNaming::uniquePrefabName(m_registry, "New Prefab"), "New Prefab");
}

TEST_F(AssetNamingTest, TakenPrefabNameGetsNumberedSuffixSkippingGaps)
{
  addPrefab("New Prefab");
  EXPECT_EQ(assetNaming::uniquePrefabName(m_registry, "New Prefab"), "New Prefab (2)");

  addPrefab("New Prefab (2)");
  addPrefab("New Prefab (4)");
  EXPECT_EQ(assetNaming::uniquePrefabName(m_registry, "New Prefab"), "New Prefab (3)");
}

TEST_F(AssetNamingTest, ASceneNameIsTakenForPrefabsAndTheOtherWayAround)
{
  addScene("New Prefab");
  EXPECT_TRUE(assetNaming::isNameTaken(m_registry, AssetType::Prefab, "New Prefab"));
  EXPECT_EQ(assetNaming::uniquePrefabName(m_registry, "New Prefab"), "New Prefab (2)");

  addPrefab("New Scene");
  EXPECT_TRUE(assetNaming::isNameTaken(m_registry, AssetType::Scene, "New Scene"));
  EXPECT_EQ(assetNaming::uniqueSceneName(m_registry, "New Scene"), "New Scene (2)");
  EXPECT_FALSE(assetNaming::isNameTaken(m_registry, AssetType::Scene, "Unused"));
}

TEST_F(AssetNamingTest, ScriptsKeepTheirOwnNameSpace)
{
  addScene("Shared");
  addPrefab("Shared");
  EXPECT_FALSE(assetNaming::isNameTaken(m_registry, AssetType::Script, "Shared"));
}

TEST_F(AssetNamingTest, DistinctSceneAndPrefabNamesAreBothRegistered)
{
  const auto sceneUUID = makeUUID(10);
  const auto prefabUUID = makeUUID(11);
  m_registry.registerAsset({ .uuid = sceneUUID, .type = AssetType::Scene, .path = "Level" });
  m_registry.registerAsset({ .uuid = prefabUUID, .type = AssetType::Prefab, .path = "Block",
                             .body = assetNaming::emptyPrefabBody("Block", uuids::to_string(makeUUID(903))) });
  EXPECT_NE(m_registry.getByUUID(sceneUUID), nullptr);
  EXPECT_NE(m_registry.getByUUID(prefabUUID), nullptr);
}

TEST_F(AssetNamingTest, RegistryDropsAPrefabSharingASceneName)
{
  const auto sceneUUID = makeUUID(20);
  const auto prefabUUID = makeUUID(21);
  m_registry.registerAsset({ .uuid = sceneUUID, .type = AssetType::Scene, .path = "Same" });
  m_registry.registerAsset({ .uuid = prefabUUID, .type = AssetType::Prefab, .path = "Same",
                             .body = assetNaming::emptyPrefabBody("Same", uuids::to_string(makeUUID(904))) });
  EXPECT_NE(m_registry.getByUUID(sceneUUID), nullptr);
  EXPECT_EQ(m_registry.getByUUID(prefabUUID), nullptr);
}

TEST_F(AssetNamingTest, EmptyPrefabBodyInstantiatesAsOneObjectWithATransform)
{
  const std::string body = assetNaming::emptyPrefabBody("Fresh", uuids::to_string(makeUUID(900)));
  ASSERT_FALSE(body.empty());
  const auto parsed = nlohmann::json::parse(body);

  fixtures::Scene scene;
  const auto object = scene.objectManager->instantiate(parsed);
  ASSERT_NE(object, nullptr);

  EXPECT_EQ(object->getName(), "Fresh");
  EXPECT_TRUE(object->getChildren().empty());
  EXPECT_NE(object->getComponent<Transform>(ComponentType::transform), nullptr);
  EXPECT_EQ(scene.objectManager->getAllObjects().size(), 1u);
}

TEST_F(AssetNamingTest, ReRegisteringAPrefabNameKeepsTheUUIDAndUpdatesTheBody)
{
  const auto first = makeUUID(1);
  const auto second = makeUUID(2);
  m_registry.registerAsset({ .uuid = first, .type = AssetType::Prefab, .path = "New Prefab",
                             .body = assetNaming::emptyPrefabBody("New Prefab", uuids::to_string(makeUUID(901))) });
  m_registry.registerAsset({ .uuid = second, .type = AssetType::Prefab, .path = "New Prefab",
                             .body = assetNaming::emptyPrefabBody("Renamed", uuids::to_string(makeUUID(902))) });

  ASSERT_NE(m_registry.getByUUID(first), nullptr);
  EXPECT_EQ(m_registry.getByUUID(second), nullptr);
  EXPECT_EQ(m_registry.getPrefabBody(first).at("name"), "Renamed");
}

class AssetNamingFileTest : public AssetNamingTest
{
protected:
  void SetUp() override
  {
    std::mt19937 rng{ std::random_device{}() };
    m_dir = std::filesystem::temp_directory_path() / ("ecs3d_asset_naming_" + std::to_string(rng()));

    std::error_code ec;
    std::filesystem::remove_all(m_dir, ec);
    std::filesystem::create_directories(m_dir);
  }

  void TearDown() override
  {
    std::error_code ec;
    std::filesystem::remove_all(m_dir, ec);
  }

  std::filesystem::path m_dir;
};

TEST_F(AssetNamingFileTest, ScriptNameSkipsAnExistingFile)
{
  const auto fileExists = [this](const std::string& name) {
    return std::filesystem::exists(m_dir / (name + ".cs"));
  };

  EXPECT_EQ(assetNaming::uniqueScriptName(m_registry, "NewScript", fileExists), "NewScript");

  std::ofstream(m_dir / "NewScript.cs") << "// existing\n";
  std::ofstream(m_dir / "NewScript2.cs") << "// existing\n";

  EXPECT_EQ(assetNaming::uniqueScriptName(m_registry, "NewScript", fileExists), "NewScript3");
}
