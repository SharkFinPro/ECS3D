#include <gtest/gtest.h>

#include <AssetNaming.h>
#include <assets/AssetRegistry.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
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

TEST_F(AssetNamingTest, ScriptNameSkipsAnExistingFile)
{
  std::mt19937 rng{ std::random_device{}() };
  const auto dir = std::filesystem::temp_directory_path() / ("ecs3d_asset_naming_" + std::to_string(rng()));
  std::filesystem::create_directories(dir);

  const auto fileExists = [&dir](const std::string& name) {
    return std::filesystem::exists(dir / (name + ".cs"));
  };

  EXPECT_EQ(assetNaming::uniqueScriptName(m_registry, "NewScript", fileExists), "NewScript");

  std::ofstream(dir / "NewScript.cs") << "// existing\n";
  std::ofstream(dir / "NewScript2.cs") << "// existing\n";

  EXPECT_EQ(assetNaming::uniqueScriptName(m_registry, "NewScript", fileExists), "NewScript3");

  std::filesystem::remove_all(dir);
}
