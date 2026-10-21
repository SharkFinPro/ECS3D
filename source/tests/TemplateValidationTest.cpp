#include <gtest/gtest.h>

#include "TemplateValidation.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <random>
#include <set>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
  const std::filesystem::path sourceDir = std::filesystem::path(ECS3D_SOURCE_DIR) / "source";

  templateValidation::Context context()
  {
    return { sourceDir / "apps" / "server" / "defaultAssets", sourceDir / "libs" / "scripting" / "UserScripts" };
  }

  std::filesystem::path fixtureFolder()
  {
    return sourceDir / "tests" / "fixtures" / "templates" / "valid-fixture";
  }

  bool hasProblem(const std::vector<std::string>& problems, const std::string& tag)
  {
    return std::any_of(problems.begin(), problems.end(), [&](const std::string& problem) {
      return problem.rfind(tag, 0) == 0;
    });
  }
}

class TemplateValidation : public ::testing::Test {
protected:
  void SetUp() override
  {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    m_root = std::filesystem::temp_directory_path()
             / ("ecs3d-template-" + std::string(info->name()) + "-" + std::to_string(std::random_device{}()));

    std::error_code ec;
    std::filesystem::remove_all(m_root, ec);
    std::filesystem::copy(fixtureFolder(), m_root, std::filesystem::copy_options::recursive, ec);
    ASSERT_FALSE(ec) << ec.message();
  }

  void TearDown() override
  {
    std::error_code ec;
    std::filesystem::remove_all(m_root, ec);
  }

  [[nodiscard]] nlohmann::json readJSON(const char* name) const
  {
    std::ifstream file(m_root / name);
    return nlohmann::json::parse(file);
  }

  void writeJSON(const char* name, const nlohmann::json& data) const
  {
    std::ofstream(m_root / name) << data.dump(2);
  }

  template <typename Edit>
  void editProject(Edit edit) const
  {
    auto project = readJSON("project.ecs3d");
    edit(project);
    writeJSON("project.ecs3d", project);
  }

  template <typename Edit>
  void editManifest(Edit edit) const
  {
    auto manifest = readJSON("template.json");
    edit(manifest);
    writeJSON("template.json", manifest);
  }

  void expectOnly(const std::string& tag) const
  {
    const auto problems = templateValidation::validate(m_root, context());
    EXPECT_TRUE(hasProblem(problems, tag)) << "expected a " << tag << " problem";
    EXPECT_EQ(problems.size(), 1u) << "expected exactly one problem";
  }

  std::filesystem::path m_root;
};

TEST(TemplateValidationShipped, EveryShippedTemplateValidates)
{
  const auto templates = sourceDir / "templates";
  ASSERT_TRUE(std::filesystem::is_directory(templates));

  std::set<std::string> ids;
  for (const auto& entry : std::filesystem::directory_iterator(templates))
  {
    if (!entry.is_directory())
    {
      continue;
    }

    SCOPED_TRACE(entry.path().filename().string());

    if (const auto manifest = loadTemplateManifest(entry.path()))
    {
      EXPECT_EQ(manifest->id, entry.path().filename().string());
      EXPECT_TRUE(ids.insert(manifest->id).second) << "duplicate id " << manifest->id;
    }

    for (const auto& problem : templateValidation::validate(entry.path(), context()))
    {
      ADD_FAILURE() << problem;
    }
  }
}

TEST_F(TemplateValidation, TheFixtureValidatesCleanly)
{
  const auto problems = templateValidation::validate(m_root, context());

  for (const auto& problem : problems)
  {
    ADD_FAILURE() << problem;
  }
}

TEST_F(TemplateValidation, AMissingAssetFileIsReported)
{
  editProject([](nlohmann::json& project) {
    project["assets"]["models"][0]["filePath"] = "assets/models/not_there.glb";
  });

  expectOnly("asset-file:");
}

TEST_F(TemplateValidation, ATemplateOwnedAssetFileIsFoundInTheFolder)
{
  std::filesystem::create_directories(m_root / "assets" / "models");
  std::ofstream(m_root / "assets" / "models" / "mine.glb") << "x";
  editProject([](nlohmann::json& project) {
    project["assets"]["models"][0]["filePath"] = "assets/models/mine.glb";
  });

  EXPECT_TRUE(templateValidation::validate(m_root, context()).empty());
}

TEST_F(TemplateValidation, AnEntrySceneThatIsNotInTheProjectIsReported)
{
  editManifest([](nlohmann::json& manifest) { manifest["entryScene"] = "Nowhere"; });

  expectOnly("entryScene:");
}

TEST_F(TemplateValidation, AnUnknownScriptClassIsReported)
{
  editProject([](nlohmann::json& project) {
    project["assets"]["scenes"][0]["objects"][0]["scripts"][0]["className"] = "NoSuchScript";
  });

  expectOnly("script-class:");
}

TEST_F(TemplateValidation, ATemplateOwnedScriptClassResolves)
{
  std::filesystem::create_directories(m_root / "scripts");
  std::ofstream(m_root / "scripts" / "Mine.cs") << "public class Mine : ScriptBase {}";
  editProject([](nlohmann::json& project) {
    project["assets"]["scenes"][0]["objects"][0]["scripts"][0]["className"] = "Mine";
  });

  EXPECT_TRUE(templateValidation::validate(m_root, context()).empty());
}

TEST_F(TemplateValidation, AScriptFieldHoldingAnObjectUuidIsReported)
{
  editProject([](nlohmann::json& project) {
    auto& object = project["assets"]["scenes"][0]["objects"][0];
    object["scripts"][0]["fields"][0]["value"] = object["uuid"];
  });

  expectOnly("script-uuid:");
}

TEST_F(TemplateValidation, AScriptFieldHoldingAPrefabBodyObjectUuidIsReported)
{
  editProject([](nlohmann::json& project) {
    const auto bodyUUID = project["assets"]["prefabs"][0]["body"]["uuid"];
    project["assets"]["scenes"][0]["objects"][0]["scripts"][0]["fields"][0]["value"] = bodyUUID;
  });

  expectOnly("script-uuid:");
}

TEST_F(TemplateValidation, AModelRendererReferenceThatIsNotRegisteredIsReported)
{
  editProject([](nlohmann::json& project) {
    auto& renderer = project["assets"]["prefabs"][0]["body"]["components"][1];
    renderer["modelUUID"] = "99999999-9999-4999-8999-999999999999";
  });

  expectOnly("reference:");
}

TEST_F(TemplateValidation, AMissingRequiredManifestFieldIsReported)
{
  editManifest([](nlohmann::json& manifest) { manifest.erase("description"); });

  expectOnly("manifest:");
}

TEST_F(TemplateValidation, InvertedPlayerCountsAreReported)
{
  editManifest([](nlohmann::json& manifest) { manifest["players"] = { { "min", 4 }, { "max", 2 } }; });

  expectOnly("manifest:");
}

TEST_F(TemplateValidation, AMissingThumbnailIsReported)
{
  std::filesystem::remove(m_root / "thumbnail.png");

  expectOnly("thumbnail:");
}

TEST_F(TemplateValidation, AMissingProjectFileIsReported)
{
  std::filesystem::remove(m_root / "project.ecs3d");

  expectOnly("project:");
}

TEST_F(TemplateValidation, AProjectThatDoesNotLoadIsReported)
{
  editProject([](nlohmann::json& project) { project["assets"]["scenes"][0].erase("objects"); });

  expectOnly("project:");
}

TEST_F(TemplateValidation, ASceneObjectTextureThatIsNotRegisteredIsReported)
{
  editProject([](nlohmann::json& project) {
    auto& renderer = project["assets"]["scenes"][0]["objects"][0]["components"][1];
    renderer["textureUUID"] = "99999999-9999-4999-8999-999999999999";
  });

  expectOnly("reference:");
}

TEST_F(TemplateValidation, AChildObjectModelThatIsNotRegisteredIsReported)
{
  editProject([](nlohmann::json& project) {
    auto child = project["assets"]["prefabs"][0]["body"];
    child["uuid"] = "88888888-8888-4888-8888-888888888888";
    child["components"][1]["modelUUID"] = "99999999-9999-4999-8999-999999999999";
    project["assets"]["scenes"][0]["objects"][0]["children"].push_back(child);
  });

  expectOnly("reference:");
}

TEST_F(TemplateValidation, UuidCaseDoesNotMatter)
{
  editProject([](nlohmann::json& project) {
    project["assets"]["models"][0]["uuid"] = "AAAAAAAA-AAAA-4AAA-8AAA-AAAAAAAAAAAA";
    project["assets"]["scenes"][0]["objects"][0]["components"][1]["modelUUID"] =
      "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
    project["assets"]["prefabs"][0]["body"]["components"][1]["modelUUID"] =
      "AAAAAAAA-AAAA-4AAA-8AAA-AAAAAAAAAAAA";
  });

  EXPECT_TRUE(templateValidation::validate(m_root, context()).empty());
}

TEST_F(TemplateValidation, ATemplateScriptDuplicatingABuiltInClassIsReported)
{
  std::filesystem::create_directories(m_root / "scripts");
  std::ofstream(m_root / "scripts" / "Copy.cs") << "public class PlayerScript : ScriptBase {}";

  expectOnly("script-class:");
}

TEST_F(TemplateValidation, AScriptFieldNamingAPrefabResolves)
{
  editProject([](nlohmann::json& project) {
    project["assets"]["scenes"][0]["objects"][0]["scripts"][0]["fields"][0]["value"] =
      project["assets"]["prefabs"][0]["uuid"];
  });

  EXPECT_TRUE(templateValidation::validate(m_root, context()).empty());
}

TEST_F(TemplateValidation, AScriptFieldNamingNothingRegisteredIsReported)
{
  editProject([](nlohmann::json& project) {
    project["assets"]["scenes"][0]["objects"][0]["scripts"][0]["fields"][0]["value"] =
      "99999999-9999-4999-8999-999999999999";
  });

  expectOnly("script-uuid:");
}

TEST_F(TemplateValidation, AnAssetPathEscapingTheFolderIsReported)
{
  editProject([](nlohmann::json& project) {
    project["assets"]["models"][0]["filePath"] = "../valid-fixture/template.json";
  });

  expectOnly("asset-file:");
}

TEST_F(TemplateValidation, AnAbsoluteThumbnailIsReported)
{
  editManifest([&](nlohmann::json& manifest) { manifest["thumbnail"] = (m_root / "thumbnail.png").string(); });

  expectOnly("thumbnail:");
}

TEST_F(TemplateValidation, AnEmptyProjectObjectIsReportedNotThrown)
{
  writeJSON("project.ecs3d", nlohmann::json::object());

  expectOnly("project:");
}

TEST_F(TemplateValidation, AWronglyTypedAssetListIsReportedNotThrown)
{
  editProject([](nlohmann::json& project) { project["assets"]["models"] = "none"; });

  expectOnly("project:");
}

namespace
{
  nlohmann::json cubeHullCollider(const std::string& sourceModel)
  {
    nlohmann::json vertices = nlohmann::json::array();
    for (unsigned i = 0; i < 8; ++i)
    {
      vertices.push_back((i & 1u) ? 1.0 : -1.0);
      vertices.push_back((i & 2u) ? 1.0 : -1.0);
      vertices.push_back((i & 4u) ? 1.0 : -1.0);
    }

    return {
      { "type", "Collider" },
      { "subType", "ConvexMesh" },
      { "position", { 0, 0, 0 } },
      { "rotation", { 0, 0, 0 } },
      { "scale", { 1, 1, 1 } },
      { "sourceModel", sourceModel },
      { "vertices", vertices },
      { "indices", { 1, 3, 7, 1, 7, 5, 0, 4, 6, 0, 6, 2, 2, 7, 3, 2, 6, 7,
                     0, 1, 5, 0, 5, 4, 4, 5, 7, 4, 7, 6, 0, 3, 1, 0, 2, 3 } }
    };
  }
}

TEST_F(TemplateValidation, AConvexSourceModelThatIsRegisteredResolves)
{
  editProject([](nlohmann::json& project) {
    project["assets"]["scenes"][0]["objects"][0]["components"].push_back(
      cubeHullCollider("11111111-1111-4111-8111-111111111111"));
  });

  EXPECT_TRUE(templateValidation::validate(m_root, context()).empty());
}

TEST_F(TemplateValidation, AConvexSourceModelThatIsNotRegisteredIsReported)
{
  editProject([](nlohmann::json& project) {
    project["assets"]["scenes"][0]["objects"][0]["components"].push_back(
      cubeHullCollider("99999999-9999-4999-8999-999999999999"));
  });

  expectOnly("reference:");
}
