#include <gtest/gtest.h>

#include <ProjectOrigin.h>
#include <ProjectPaths.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <string>
#include <system_error>

namespace {
  namespace fs = std::filesystem;

  class ProjectOriginTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
      static std::atomic<int> counter{0};
      const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
      std::random_device device;
      m_dir = fs::temp_directory_path() / ("ecs3d_project_origin_" + std::string(info->name()) + "_"
        + std::to_string(device()) + "_" + std::to_string(counter++));
      fs::remove_all(m_dir);
      fs::create_directories(m_dir);
    }

    void TearDown() override
    {
      std::error_code error;
      fs::remove_all(m_dir, error);
    }

    fs::path touch(const fs::path& relative, const std::string& content = "x") const
    {
      const auto path = m_dir / relative;
      fs::create_directories(path.parent_path());
      std::ofstream(path) << content;
      return path;
    }

    fs::path makeTemplate(const std::string& name) const
    {
      touch("tpl/project.ecs3d", "{}");
      touch("tpl/assets/models/rock.glb", "rock");
      touch("tpl/scripts/Mover.cs", "script");
      touch("tpl/template.json",
            R"({"id":"empty-3d","name":")" + name + R"(","description":"d","category":"template","tags":[],)"
            R"("thumbnail":"t.png","entryScene":"Main","minEngineVersion":"0.0.1"})");
      return m_dir / "tpl";
    }

    static std::set<std::string> listing(const fs::path& root)
    {
      std::set<std::string> files;
      for (const auto& entry : fs::recursive_directory_iterator(root))
      {
        files.insert(fs::relative(entry.path(), root).generic_string() + (entry.is_directory() ? "/" : ""));
      }
      return files;
    }

    fs::path m_dir;
  };
}

TEST_F(ProjectOriginTest, NoProjectFileIsTheBuiltInTemplate)
{
  EXPECT_TRUE(isTemplateProject(locateProject({})));
}

TEST_F(ProjectOriginTest, FolderWithManifestIsATemplateAndWithoutIsNot)
{
  const auto tpl = makeTemplate("Empty 3D");
  touch("mine/project.ecs3d", "{}");

  EXPECT_TRUE(isTemplateProject(locateProject(tpl)));
  EXPECT_TRUE(isTemplateProject(locateProject(tpl / "project.ecs3d")));
  EXPECT_FALSE(isTemplateProject(locateProject(m_dir / "mine")));
}

TEST_F(ProjectOriginTest, LegacyJsonFileIsNotATemplate)
{
  const auto legacy = touch("old.json", "{}");

  EXPECT_FALSE(isTemplateProject(locateProject(legacy)));
  EXPECT_TRUE(isTemplateProject(locateProject({})));
}

TEST_F(ProjectOriginTest, DisplayNameIsTheManifestNameThenTheFolderName)
{
  const auto tpl = makeTemplate("Empty 3D");
  EXPECT_EQ(templateDisplayName(locateProject(tpl)), std::optional<std::string>("Empty 3D"));

  touch("broken/project.ecs3d", "{}");
  touch("broken/template.json", "not json");
  EXPECT_EQ(templateDisplayName(locateProject(m_dir / "broken")), std::optional<std::string>("broken"));

  EXPECT_FALSE(templateDisplayName(locateProject({})).has_value());
}

TEST_F(ProjectOriginTest, UntitledTitleNamesTheTemplateOrTheBuiltInProject)
{
  EXPECT_EQ(untitledTitle(std::string("Coin Rush")), "Untitled (from Coin Rush)");
  EXPECT_EQ(untitledTitle(std::nullopt), "Untitled (from the built-in project)");
}

TEST_F(ProjectOriginTest, DefaultSaveFolderTakesTheFirstFreeName)
{
  const std::set<fs::path> taken = { m_dir / "Empty 3D", m_dir / "Empty 3D 2" };
  const auto isTaken = [&taken](const fs::path& folder) { return taken.count(folder) > 0; };

  EXPECT_EQ(defaultSaveFolder(m_dir, "Sandbox", isTaken), m_dir / "Sandbox");
  EXPECT_EQ(defaultSaveFolder(m_dir, "Empty 3D", isTaken), m_dir / "Empty 3D 3");
}

TEST_F(ProjectOriginTest, DefaultSaveFolderCleansTheName)
{
  EXPECT_EQ(defaultSaveFolder(m_dir, "A/B:C?"), m_dir / "ABC");
  EXPECT_EQ(defaultSaveFolder(m_dir, "A\\B|C"), m_dir / "ABC");
  EXPECT_EQ(defaultSaveFolder(m_dir, "  "), m_dir / "New Project");
  EXPECT_EQ(defaultSaveFolder(m_dir, "???"), m_dir / "New Project");
  EXPECT_EQ(defaultSaveFolder(m_dir, "A<B>C\"D*E"), m_dir / "ABCDE");
  EXPECT_EQ(defaultSaveFolder(m_dir, "A B"), m_dir / "A B");
  EXPECT_EQ(defaultSaveFolder(m_dir, "A\x1F"), m_dir / "A");
  EXPECT_EQ(defaultSaveFolder(m_dir, "Name. ."), m_dir / "Name");
}

TEST_F(ProjectOriginTest, DefaultSaveFolderKeepsNonAsciiBytes)
{
  const std::string name = "Caf\xC3\xA9";
  const auto folder = defaultSaveFolder(m_dir, name);

  EXPECT_EQ(pathToUtf8(folder.filename()), name);
}

TEST_F(ProjectOriginTest, DefaultSaveFolderAvoidsDeviceNames)
{
  EXPECT_EQ(defaultSaveFolder(m_dir, "con"), m_dir / "con Project");
  EXPECT_EQ(defaultSaveFolder(m_dir, "NUL.txt"), m_dir / "NUL.txt Project");
  EXPECT_EQ(defaultSaveFolder(m_dir, "Com1"), m_dir / "Com1 Project");
  EXPECT_EQ(defaultSaveFolder(m_dir, "LPT9"), m_dir / "LPT9 Project");
  EXPECT_EQ(defaultSaveFolder(m_dir, "prn"), m_dir / "prn Project");
  EXPECT_EQ(defaultSaveFolder(m_dir, "Aux"), m_dir / "Aux Project");
  EXPECT_EQ(defaultSaveFolder(m_dir, "Com0"), m_dir / "Com0");
  EXPECT_EQ(defaultSaveFolder(m_dir, "Console"), m_dir / "Console");
}

TEST_F(ProjectOriginTest, DefaultSaveFolderCapsTheNameLength)
{
  const auto folder = defaultSaveFolder(m_dir, std::string(200, 'a'));
  EXPECT_EQ(pathToUtf8(folder.filename()), std::string(64, 'a'));

  // 63 ASCII bytes then a two-byte letter would straddle the cap; the letter must be dropped whole.
  const auto cut = defaultSaveFolder(m_dir, std::string(63, 'a') + "\xC3\xA9\xC3\xA9");
  EXPECT_EQ(pathToUtf8(cut.filename()), std::string(63, 'a'));
}

TEST_F(ProjectOriginTest, FolderHoldingOnlyEmptyProjectSubfoldersIsFree)
{
  fs::create_directories(m_dir / "retry" / "assets");
  fs::create_directories(m_dir / "retry" / "scripts");
  EXPECT_TRUE(folderIsFreeForNewProject(m_dir / "retry"));

  touch("retry/assets/a.glb");
  EXPECT_FALSE(folderIsFreeForNewProject(m_dir / "retry"));

  fs::create_directories(m_dir / "other" / "elsewhere");
  EXPECT_FALSE(folderIsFreeForNewProject(m_dir / "other"));
}

TEST_F(ProjectOriginTest, AssetWritesAreBlockedOnlyOnAnUnsavedTemplate)
{
  EXPECT_TRUE(assetWritesBlocked(true, false));
  EXPECT_FALSE(assetWritesBlocked(true, true));
  EXPECT_FALSE(assetWritesBlocked(false, false));
  EXPECT_FALSE(assetWritesBlocked(false, true));
}

TEST_F(ProjectOriginTest, DefaultSaveFolderSkipsFilledFoldersButNotEmptyOnes)
{
  touch("Empty 3D/project.ecs3d", "{}");
  EXPECT_EQ(defaultSaveFolder(m_dir, "Empty 3D"), m_dir / "Empty 3D 2");

  fs::create_directories(m_dir / "Empty 3D 2");
  EXPECT_EQ(defaultSaveFolder(m_dir, "Empty 3D"), m_dir / "Empty 3D 2");
}

TEST_F(ProjectOriginTest, FolderIsFreeWhenMissingOrEmptyOnly)
{
  fs::create_directories(m_dir / "empty");
  touch("full/a.txt");

  EXPECT_TRUE(folderIsFreeForNewProject(m_dir / "missing"));
  EXPECT_TRUE(folderIsFreeForNewProject(m_dir / "empty"));
  EXPECT_FALSE(folderIsFreeForNewProject(m_dir / "full"));
  EXPECT_FALSE(folderIsFreeForNewProject(m_dir / "full" / "a.txt"));
}

TEST_F(ProjectOriginTest, SaveActsAsSaveAsUntilTheTemplateIsSavedToAFolder)
{
  EXPECT_TRUE(saveActsAsSaveAs(true, false));
  EXPECT_FALSE(saveActsAsSaveAs(true, true));
  EXPECT_FALSE(saveActsAsSaveAs(false, false));
  EXPECT_FALSE(saveActsAsSaveAs(false, true));
}

TEST_F(ProjectOriginTest, TemplateSaveRefusesTheTemplateItsSubfoldersAndFilledFolders)
{
  const auto tpl = makeTemplate("Empty 3D");
  touch("full/a.txt");

  EXPECT_FALSE(templateSaveRefusal(m_dir / "fresh", tpl).has_value());
  EXPECT_EQ(templateSaveRefusal(tpl, tpl), "the folder is not empty");
  EXPECT_EQ(templateSaveRefusal(m_dir / "full", tpl), "the folder is not empty");

  // An empty template folder is free, so only the containment check can refuse it or anything inside it.
  const auto emptyTemplate = m_dir / "EmptyTemplate";
  fs::create_directories(emptyTemplate);
  const std::string inside = "the folder is the template's own folder or lies inside it";
  EXPECT_EQ(templateSaveRefusal(emptyTemplate, emptyTemplate), inside);
  EXPECT_EQ(templateSaveRefusal(emptyTemplate / "newfolder", emptyTemplate), inside);
  EXPECT_EQ(templateSaveRefusal(emptyTemplate / "assets" / "deeper", emptyTemplate), inside);
  EXPECT_FALSE(templateSaveRefusal(m_dir / "EmptyTemplateSibling", emptyTemplate).has_value());
}

// SaveUI::saveToFolder is the sequence below around templateSaveRefusal; it needs a window, so this pins
// the contract of the helpers it calls: the template's folder is byte-for-byte unchanged, its manifest is
// not copied, and the new folder holds the project's assets and scripts.
TEST_F(ProjectOriginTest, SavingATemplateIntoAFreshFolderLeavesTheTemplateUntouched)
{
  const auto tpl = makeTemplate("Empty 3D");
  const auto before = listing(tpl);
  const auto target = m_dir / "out" / "Empty 3D";

  ASSERT_FALSE(templateSaveRefusal(target, tpl).has_value());

  std::error_code error;
  ASSERT_TRUE(ensureProjectFolders(target, error));
  copyProjectContent(tpl, target, error);
  ASSERT_FALSE(error);
  std::ofstream(projectFileInFolder(target)) << "{}";

  EXPECT_EQ(listing(tpl), before);
  EXPECT_TRUE(fs::exists(target / "assets" / "models" / "rock.glb"));
  EXPECT_TRUE(fs::exists(target / "scripts" / "Mover.cs"));
  EXPECT_TRUE(fs::exists(target / "project.ecs3d"));
  EXPECT_FALSE(fs::exists(target / "template.json"));
  EXPECT_FALSE(isTemplateProject(locateProject(target)));
}
