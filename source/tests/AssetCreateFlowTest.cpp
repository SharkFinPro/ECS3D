#include <gtest/gtest.h>

#include <AssetCreateFlow.h>
#include <assets/AssetRegistry.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

namespace {
  uuids::uuid makeUUID(const unsigned n)
  {
    char text[40];
    std::snprintf(text, sizeof(text), "%08x-0000-0000-0000-000000000000", n);
    return uuids::uuid::from_string(text).value();
  }

  const auto noFiles = [](const std::string&) { return false; };

  constexpr const char* invalidName = "Enter a valid name (no path characters).";

  class AssetCreateFlowTest : public testing::Test
  {
  protected:
    void addScene(const std::string& name)
    {
      m_registry.registerAsset({ .uuid = makeUUID(++m_next), .type = AssetType::Scene, .path = name });
    }

    void addPrefab(const std::string& name)
    {
      m_registry.registerAsset({ .uuid = makeUUID(++m_next), .type = AssetType::Prefab, .path = name,
                                 .body = "{}" });
    }

    void addScript(const std::string& className)
    {
      m_registry.registerAsset({ .uuid = makeUUID(++m_next), .type = AssetType::Script,
                                 .path = "scripts/UserScripts/" + className + ".cs", .className = className });
    }

    void addModel(const std::string& path)
    {
      m_registry.registerAsset({ .uuid = makeUUID(++m_next), .type = AssetType::Model, .path = path,
                                 .displayName = path });
    }

    AssetRegistry m_registry;
    AssetCreateFlow m_flow;
    unsigned m_next = 0;
  };
}

TEST_F(AssetCreateFlowTest, BeginReplacesThePendingRequestAndClearsTheError)
{
  m_flow.begin(AssetCreateFlow::Type::Model, "C:/art/rock.glb", "rock");
  m_flow.pending().name[0] = '\0';
  EXPECT_FALSE(m_flow.validate(m_registry, noFiles));
  ASSERT_FALSE(m_flow.error().empty());

  m_flow.begin(AssetCreateFlow::Type::Scene, "", "Level");
  EXPECT_TRUE(m_flow.error().empty());
  EXPECT_EQ(m_flow.pending().type, AssetCreateFlow::Type::Scene);
  EXPECT_TRUE(m_flow.pending().sourcePath.empty());
  EXPECT_STREQ(m_flow.pending().name, "Level");
}

TEST_F(AssetCreateFlowTest, BeginKeepsTheSourcePath)
{
  m_flow.begin(AssetCreateFlow::Type::Texture, "C:/art/wood.png", "wood");
  EXPECT_EQ(m_flow.pending().sourcePath, "C:/art/wood.png");
  EXPECT_STREQ(m_flow.pending().name, "wood");
}

TEST_F(AssetCreateFlowTest, BeginTruncatesALongNameToTheBufferAndTerminatesIt)
{
  const std::string longName(300, 'x');
  m_flow.begin(AssetCreateFlow::Type::Scene, "", longName);

  const auto& pending = m_flow.pending();
  EXPECT_EQ(std::strlen(pending.name), sizeof(pending.name) - 1);
  EXPECT_EQ(pending.name[sizeof(pending.name) - 1], '\0');
  EXPECT_EQ(std::string(pending.name), std::string(sizeof(pending.name) - 1, 'x'));
}

TEST_F(AssetCreateFlowTest, TheOpenRequestIsTakenExactlyOncePerBegin)
{
  EXPECT_FALSE(m_flow.takeOpenRequest());

  m_flow.begin(AssetCreateFlow::Type::Scene, "", "A");
  EXPECT_TRUE(m_flow.takeOpenRequest());
  EXPECT_FALSE(m_flow.takeOpenRequest());

  m_flow.begin(AssetCreateFlow::Type::Scene, "", "B");
  EXPECT_TRUE(m_flow.takeOpenRequest());
}

TEST_F(AssetCreateFlowTest, AnInvalidNameIsRefusedForEveryPathCharacterAndAnEmptyName)
{
  for (const char c : std::string("/\\:*?\"<>|"))
  {
    m_flow.begin(AssetCreateFlow::Type::Scene, "", std::string("a") + c + "b");
    EXPECT_FALSE(m_flow.validate(m_registry, noFiles)) << c;
    EXPECT_EQ(m_flow.error(), invalidName) << c;
  }

  m_flow.begin(AssetCreateFlow::Type::Scene, "", "");
  EXPECT_FALSE(m_flow.validate(m_registry, noFiles));
  EXPECT_EQ(m_flow.error(), invalidName);

  m_flow.begin(AssetCreateFlow::Type::Scene, "", "A fine name (2)");
  EXPECT_TRUE(m_flow.validate(m_registry, noFiles));
}

TEST_F(AssetCreateFlowTest, AnInvalidNameWinsOverATakenOne)
{
  addScript("Bad:Name");

  m_flow.begin(AssetCreateFlow::Type::Script, "", "Bad:Name");
  EXPECT_FALSE(m_flow.validate(m_registry, [](const std::string&) { return true; }));
  EXPECT_EQ(m_flow.error(), invalidName);
}

TEST_F(AssetCreateFlowTest, ARegisteredScriptNameIsRefused)
{
  addScript("Mover");

  m_flow.begin(AssetCreateFlow::Type::Script, "", "Mover");
  EXPECT_FALSE(m_flow.validate(m_registry, noFiles));
  EXPECT_EQ(m_flow.error(), "A script named Mover already exists.");

  m_flow.begin(AssetCreateFlow::Type::Script, "", "Spinner");
  EXPECT_TRUE(m_flow.validate(m_registry, noFiles));
}

TEST_F(AssetCreateFlowTest, AnUnregisteredScriptWhoseFileExistsIsRefused)
{
  const auto fileExists = [](const std::string& name) { return name == "OnDisk"; };

  m_flow.begin(AssetCreateFlow::Type::Script, "", "OnDisk");
  EXPECT_FALSE(m_flow.validate(m_registry, fileExists));
  EXPECT_EQ(m_flow.error(), "A script named OnDisk already exists.");

  m_flow.begin(AssetCreateFlow::Type::Script, "", "NotOnDisk");
  EXPECT_TRUE(m_flow.validate(m_registry, fileExists));
}

TEST_F(AssetCreateFlowTest, TheFileCheckIsOnlyAskedForScriptsThatPassedTheRegistry)
{
  addScript("Mover");
  int asked = 0;
  const auto counting = [&asked](const std::string&) {
    ++asked;
    return false;
  };

  m_flow.begin(AssetCreateFlow::Type::Script, "", "Mover");
  EXPECT_FALSE(m_flow.validate(m_registry, counting));
  EXPECT_EQ(asked, 0);

  m_flow.begin(AssetCreateFlow::Type::Script, "", "Fresh");
  EXPECT_TRUE(m_flow.validate(m_registry, counting));
  EXPECT_EQ(asked, 1);

  m_flow.begin(AssetCreateFlow::Type::Scene, "", "Fresh");
  EXPECT_TRUE(m_flow.validate(m_registry, counting));
  EXPECT_EQ(asked, 1);
}

TEST_F(AssetCreateFlowTest, ASceneNameCollidingWithAPrefabIsRefusedAndTheOtherWayAround)
{
  addPrefab("Shared");
  addScene("Level");

  m_flow.begin(AssetCreateFlow::Type::Scene, "", "Shared");
  EXPECT_FALSE(m_flow.validate(m_registry, noFiles));
  EXPECT_EQ(m_flow.error(), "A scene or prefab named Shared already exists.");

  m_flow.begin(AssetCreateFlow::Type::Prefab, "", "Level");
  EXPECT_FALSE(m_flow.validate(m_registry, noFiles));
  EXPECT_EQ(m_flow.error(), "A scene or prefab named Level already exists.");

  m_flow.begin(AssetCreateFlow::Type::Scene, "", "Unused");
  EXPECT_TRUE(m_flow.validate(m_registry, noFiles));
}

TEST_F(AssetCreateFlowTest, ASceneOrPrefabNameDoesNotCollideWithAScriptName)
{
  addScript("Mover");

  m_flow.begin(AssetCreateFlow::Type::Scene, "", "Mover");
  EXPECT_TRUE(m_flow.validate(m_registry, noFiles));

  m_flow.begin(AssetCreateFlow::Type::Prefab, "", "Mover");
  EXPECT_TRUE(m_flow.validate(m_registry, noFiles));
}

TEST_F(AssetCreateFlowTest, ModelAndTextureNamesHaveNoTakenCheck)
{
  addModel("rock");
  addScene("rock");
  addPrefab("wood");
  addScript("wood");

  m_flow.begin(AssetCreateFlow::Type::Model, "C:/art/rock.glb", "rock");
  EXPECT_TRUE(m_flow.validate(m_registry, [](const std::string&) { return true; }));

  m_flow.begin(AssetCreateFlow::Type::Texture, "C:/art/wood.png", "wood");
  EXPECT_TRUE(m_flow.validate(m_registry, [](const std::string&) { return true; }));
}

TEST(AssetCreateFlowTypes, OnlyModelTextureAndScriptWriteFiles)
{
  using Type = AssetCreateFlow::Type;

  EXPECT_TRUE(AssetCreateFlow::writesFiles(Type::Model));
  EXPECT_TRUE(AssetCreateFlow::writesFiles(Type::Texture));
  EXPECT_TRUE(AssetCreateFlow::writesFiles(Type::Script));
  EXPECT_FALSE(AssetCreateFlow::writesFiles(Type::Scene));
  EXPECT_FALSE(AssetCreateFlow::writesFiles(Type::Prefab));
  EXPECT_FALSE(AssetCreateFlow::writesFiles(Type::None));
}

TEST(AssetCreateFlowTypes, TheScriptDirectoryFollowsTheProjectRoot)
{
  EXPECT_EQ(AssetCreateFlow::scriptDirectory({}), std::filesystem::path("scripts/UserScripts"));
  EXPECT_EQ(AssetCreateFlow::scriptDirectory("proj"), std::filesystem::path("proj") / "scripts");
}

TEST_F(AssetCreateFlowTest, ModelAndTextureJsonCarryTheImportedPath)
{
  const auto id = makeUUID(7);
  const std::string uuid = uuids::to_string(id);

  m_flow.begin(AssetCreateFlow::Type::Model, "C:/art/rock.glb", "rock");
  EXPECT_EQ(m_flow.addAssetJson(id, "assets/models/rock.glb", "proj", "unused"),
            (nlohmann::json{ { "assetType", "model" }, { "uuid", uuid }, { "path", "assets/models/rock.glb" } }));

  m_flow.begin(AssetCreateFlow::Type::Texture, "C:/art/wood.png", "wood");
  EXPECT_EQ(m_flow.addAssetJson(id, "assets/textures/wood.png", "proj", "unused"),
            (nlohmann::json{ { "assetType", "texture" }, { "uuid", uuid }, { "path", "assets/textures/wood.png" } }));
}

TEST_F(AssetCreateFlowTest, SceneJsonCarriesTheName)
{
  const auto id = makeUUID(8);

  m_flow.begin(AssetCreateFlow::Type::Scene, "", "Level 2");
  EXPECT_EQ(m_flow.addAssetJson(id, "", "proj", "unused"),
            (nlohmann::json{ { "assetType", "scene" }, { "uuid", uuids::to_string(id) }, { "name", "Level 2" } }));
}

TEST_F(AssetCreateFlowTest, PrefabJsonCarriesAnEmptyBodyNamedAfterThePrefab)
{
  const auto id = makeUUID(9);
  const std::string objectUUID = uuids::to_string(makeUUID(10));

  m_flow.begin(AssetCreateFlow::Type::Prefab, "", "Crate");
  const auto json = m_flow.addAssetJson(id, "", "proj", objectUUID);

  EXPECT_EQ(json.at("assetType"), "prefab");
  EXPECT_EQ(json.at("uuid"), uuids::to_string(id));
  EXPECT_EQ(json.at("name"), "Crate");
  EXPECT_EQ(json.at("body"), assetNaming::emptyPrefabBody("Crate", objectUUID));
}

TEST_F(AssetCreateFlowTest, ScriptJsonPathIsProjectRelativeWithARootAndTheWorkingDirectoryOneWithout)
{
  const auto id = makeUUID(11);
  const std::string uuid = uuids::to_string(id);

  m_flow.begin(AssetCreateFlow::Type::Script, "", "Mover");

  EXPECT_EQ(m_flow.addAssetJson(id, "", "C:/proj", "unused"),
            (nlohmann::json{ { "assetType", "script" }, { "uuid", uuid }, { "path", "scripts/Mover.cs" },
                             { "className", "Mover" } }));

  EXPECT_EQ(m_flow.addAssetJson(id, "", {}, "unused"),
            (nlohmann::json{ { "assetType", "script" }, { "uuid", uuid }, { "path", "scripts/UserScripts/Mover.cs" },
                             { "className", "Mover" } }));
}

TEST_F(AssetCreateFlowTest, ACommittedSceneIsHandedOutOnceAndThePendingRequestCleared)
{
  const auto id = makeUUID(12);

  m_flow.begin(AssetCreateFlow::Type::Scene, "", "Level");
  EXPECT_FALSE(m_flow.takeSceneToActivate().has_value());

  m_flow.committed(id);
  EXPECT_EQ(m_flow.pending().type, AssetCreateFlow::Type::None);
  EXPECT_STREQ(m_flow.pending().name, "");

  const auto taken = m_flow.takeSceneToActivate();
  ASSERT_TRUE(taken.has_value());
  EXPECT_EQ(*taken, id);
  EXPECT_FALSE(m_flow.takeSceneToActivate().has_value());
}

TEST_F(AssetCreateFlowTest, OnlyASceneIsRememberedForActivation)
{
  using Type = AssetCreateFlow::Type;

  for (const Type type : { Type::Model, Type::Texture, Type::Script, Type::Prefab })
  {
    m_flow.begin(type, "src", "Thing");
    m_flow.committed(makeUUID(13));
    EXPECT_EQ(m_flow.pending().type, Type::None);
    EXPECT_FALSE(m_flow.takeSceneToActivate().has_value());
  }

  // Positive control: the same sequence for a scene does hand one out.
  m_flow.begin(Type::Scene, "", "Thing");
  m_flow.committed(makeUUID(13));
  EXPECT_TRUE(m_flow.takeSceneToActivate().has_value());
}

TEST_F(AssetCreateFlowTest, AFailedCommitKeepsTheRequestSetsTheErrorAndActivatesNothing)
{
  m_flow.begin(AssetCreateFlow::Type::Scene, "", "Level");
  m_flow.commitFailed("disk full");

  EXPECT_EQ(m_flow.error(), "disk full");
  EXPECT_EQ(m_flow.pending().type, AssetCreateFlow::Type::Scene);
  EXPECT_STREQ(m_flow.pending().name, "Level");
  EXPECT_FALSE(m_flow.takeSceneToActivate().has_value());

  // Retrying the form commits it.
  m_flow.committed(makeUUID(14));
  EXPECT_TRUE(m_flow.takeSceneToActivate().has_value());
}

TEST_F(AssetCreateFlowTest, CancelClearsThePendingRequest)
{
  m_flow.begin(AssetCreateFlow::Type::Model, "C:/art/rock.glb", "rock");
  m_flow.cancel();

  EXPECT_EQ(m_flow.pending().type, AssetCreateFlow::Type::None);
  EXPECT_TRUE(m_flow.pending().sourcePath.empty());
  EXPECT_STREQ(m_flow.pending().name, "");
}
