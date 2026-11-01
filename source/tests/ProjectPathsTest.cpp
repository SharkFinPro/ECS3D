#include <gtest/gtest.h>

#include <ProjectPaths.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <system_error>

namespace {
  namespace fs = std::filesystem;

  class ProjectPathsTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
      static std::atomic<int> counter{0};
      const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
      std::random_device device;
      m_dir = fs::temp_directory_path() / ("ecs3d_project_paths_" + std::string(info->name()) + "_"
        + std::to_string(device()) + "_" + std::to_string(counter++));
      fs::remove_all(m_dir);
      fs::create_directories(m_dir);
    }

    void TearDown() override
    {
      std::error_code error;
      fs::remove_all(m_dir, error);
    }

    fs::path touch(const fs::path& relative) const
    {
      const auto path = m_dir / relative;
      fs::create_directories(path.parent_path());
      std::ofstream(path).put('x');
      return path;
    }

    fs::path m_dir;
  };

  void expectSamePath(const fs::path& actual, const fs::path& expected)
  {
    EXPECT_EQ(actual.lexically_normal(), expected.lexically_normal());
  }

  // Runs a test inside a scratch folder so a relative path it resolves or writes cannot touch the real CWD.
  class ScopedCurrentPath {
  public:
    explicit ScopedCurrentPath(const fs::path& folder)
      : m_previous(fs::current_path())
    {
      fs::create_directories(folder);
      fs::current_path(folder);
    }

    ~ScopedCurrentPath()
    {
      std::error_code error;
      fs::current_path(m_previous, error);
    }

    ScopedCurrentPath(const ScopedCurrentPath&) = delete;
    ScopedCurrentPath& operator=(const ScopedCurrentPath&) = delete;

  private:
    fs::path m_previous;
  };
}

TEST_F(ProjectPathsTest, FolderResolvesToItsProjectFile)
{
  const auto location = locateProject(m_dir);

  expectSamePath(location.projectFile, m_dir / projectFileName);
  expectSamePath(location.root, m_dir);
}

TEST_F(ProjectPathsTest, Ecs3dFileRootsAtItsParent)
{
  const auto file = touch("game/other.ecs3d");

  const auto location = locateProject(file);

  expectSamePath(location.projectFile, file);
  expectSamePath(location.root, m_dir / "game");
}

TEST_F(ProjectPathsTest, LegacyJsonHasNoRoot)
{
  const auto file = touch("project.json");

  const auto location = locateProject(file);

  expectSamePath(location.projectFile, file);
  EXPECT_TRUE(location.root.empty());
}

TEST_F(ProjectPathsTest, MissingPathIsTreatedAsAFile)
{
  const auto missing = m_dir / "nope.json";

  const auto location = locateProject(missing);

  expectSamePath(location.projectFile, missing);
  EXPECT_TRUE(location.root.empty());
}

TEST_F(ProjectPathsTest, EmptyArgumentIsEmptyLocation)
{
  const auto location = locateProject({});

  EXPECT_TRUE(location.projectFile.empty());
  EXPECT_TRUE(location.root.empty());
}

TEST_F(ProjectPathsTest, ProjectCopyWinsWhenItExists)
{
  touch("assets/models/a.glb");

  const auto resolved = resolveAssetPath(m_dir, "assets/models/a.glb");

  ASSERT_TRUE(resolved.has_value());
  expectSamePath(*resolved, m_dir / "assets/models/a.glb");
}

TEST_F(ProjectPathsTest, FallsBackToBuiltInWhenMissingFromProject)
{
  touch("assets/models/a.glb");

  const auto resolved = resolveAssetPath(m_dir, "assets/models/b.glb");

  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, fs::path("assets/models/b.glb"));
}

TEST_F(ProjectPathsTest, EmptyRootLeavesPathUnchanged)
{
  const auto resolved = resolveAssetPath({}, "assets/models/a.glb");

  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, fs::path("assets/models/a.glb"));
}

TEST_F(ProjectPathsTest, AbsolutePathIsUntouched)
{
  const auto file = touch("assets/models/a.glb");
  ASSERT_TRUE(file.is_absolute());

  const auto resolved = resolveAssetPath(m_dir / "other", file.string());

  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, file);
}

TEST_F(ProjectPathsTest, RefusesPathsThatEscape)
{
  touch("assets/x");
  ASSERT_TRUE(resolveAssetPath(m_dir, "assets/x").has_value());

  EXPECT_FALSE(resolveAssetPath(m_dir, "../x").has_value());
  EXPECT_FALSE(resolveAssetPath(m_dir, "assets/../../x").has_value());
  EXPECT_FALSE(resolveAssetPath({}, "../x").has_value());

#ifdef _WIN32
  EXPECT_FALSE(resolveAssetPath(m_dir, "\\evil").has_value());
  EXPECT_FALSE(resolveAssetPath(m_dir, "C:evil").has_value());
#endif
}

TEST_F(ProjectPathsTest, AnEmptyFilePathStaysEmptyEvenWithAProjectRoot)
{
  const auto resolved = resolveAssetPath(m_dir, "");

  ASSERT_TRUE(resolved.has_value());
  EXPECT_TRUE(resolved->empty());
}

TEST_F(ProjectPathsTest, AllowsDotDotThatStaysInside)
{
  touch("assets/x");

  const auto resolved = resolveAssetPath(m_dir, "assets/../assets/x");

  ASSERT_TRUE(resolved.has_value());
  expectSamePath(*resolved, m_dir / "assets/x");
}

TEST_F(ProjectPathsTest, BareEcs3dFileRootsAtCurrentDirectory)
{
  const auto location = locateProject("project.ecs3d");

  EXPECT_EQ(location.projectFile, fs::path("project.ecs3d"));
  EXPECT_EQ(location.root, fs::path("."));
}

TEST_F(ProjectPathsTest, Ecs3dExtensionIsCaseInsensitive)
{
  const auto location = locateProject(m_dir / "game" / "PROJECT.ECS3D");

  expectSamePath(location.root, m_dir / "game");
  EXPECT_TRUE(locateProject(m_dir / "game" / "project.json").root.empty());
}

TEST(AbsoluteProjectArgumentTest, MakesRelativeAbsoluteAndKeepsEmptyAndAbsolute)
{
  EXPECT_TRUE(fs::path(absoluteProjectArgument("games/a")).is_absolute());
  EXPECT_TRUE(absoluteProjectArgument("").empty());

  const auto absolute = fs::absolute("games/a").string();
  EXPECT_EQ(absoluteProjectArgument(absolute), absolute);
}

TEST(QuotedProjectArgumentTest, QuotesAndStripsTrailingSeparators)
{
  EXPECT_EQ(quotedProjectArgument(R"(C:\proj)"), R"("C:\proj")");
  EXPECT_EQ(quotedProjectArgument(R"(C:\proj\)"), R"("C:\proj")");
  EXPECT_EQ(quotedProjectArgument("/home/p//"), R"("/home/p")");
  EXPECT_EQ(quotedProjectArgument("proj.ecs3d"), R"("proj.ecs3d")");
}

TEST(QuotedProjectArgumentTest, DriveRootsKeepASeparatorAndGainADot)
{
  EXPECT_EQ(quotedProjectArgument(R"(C:\)"), R"("C:\.")");
  EXPECT_EQ(quotedProjectArgument("/"), R"("/.")");
}

TEST(QuotedProjectArgumentTest, OnlyATwoCharacterDriveIsARoot)
{
  EXPECT_EQ(quotedProjectArgument("foo:"), R"("foo:")");
  EXPECT_EQ(quotedProjectArgument("foo:/"), R"("foo:")");
}

TEST_F(ProjectPathsTest, ProjectFileInFolderNamesTheProjectFile)
{
  expectSamePath(projectFileInFolder(m_dir), m_dir / "project.ecs3d");
  EXPECT_EQ(locateProject(projectFileInFolder(m_dir)).root.lexically_normal(), m_dir.lexically_normal());
}

TEST(LegacyProjectFileTest, OnlyANonEcs3dFileIsLegacy)
{
  EXPECT_TRUE(isLegacyProjectFile("old/project.json"));
  EXPECT_TRUE(isLegacyProjectFile("game"));
  EXPECT_FALSE(isLegacyProjectFile("new/project.ecs3d"));
  EXPECT_FALSE(isLegacyProjectFile("new/PROJECT.ECS3D"));
  EXPECT_FALSE(isLegacyProjectFile(""));
}

TEST_F(ProjectPathsTest, EnsureProjectFoldersCreatesAssetsAndScripts)
{
  std::error_code error;
  const auto root = m_dir / "My Game";

  ASSERT_TRUE(ensureProjectFolders(root, error));
  EXPECT_TRUE(fs::is_directory(root / "assets"));
  EXPECT_TRUE(fs::is_directory(root / "scripts"));
  EXPECT_TRUE(ensureProjectFolders(root, error));
}

TEST_F(ProjectPathsTest, EnsureProjectFoldersReportsAFolderItCannotCreate)
{
  const auto file = touch("blocker");
  std::error_code error;

  EXPECT_FALSE(ensureProjectFolders(file / "x", error));
  EXPECT_TRUE(error);
}

TEST_F(ProjectPathsTest, ImportDestinationPutsAModelUnderAssetsModels)
{
  const auto destination = importDestination(m_dir, "C:/downloads/Rock.GLB", "boulder", ImportKind::model);

  EXPECT_EQ(destination.relative, "assets/models/boulder.GLB");
  expectSamePath(destination.absolute, m_dir / "assets" / "models" / "boulder.GLB");
}

TEST_F(ProjectPathsTest, ImportDestinationPutsATextureUnderAssetsTextures)
{
  const auto destination = importDestination(m_dir, "wood.png", "", ImportKind::texture);

  EXPECT_EQ(destination.relative, "assets/textures/wood.png");
  expectSamePath(destination.absolute, m_dir / "assets" / "textures" / "wood.png");
}

TEST_F(ProjectPathsTest, ImportDestinationSuffixesANameThatIsTaken)
{
  touch("assets/models/rock.glb");

  EXPECT_EQ(importDestination(m_dir, "x/rock.glb", "rock", ImportKind::model).relative, "assets/models/rock-1.glb");

  touch("assets/models/rock-1.glb");
  const auto third = importDestination(m_dir, "x/rock.glb", "rock", ImportKind::model);

  EXPECT_EQ(third.relative, "assets/models/rock-2.glb");
  expectSamePath(third.absolute, m_dir / "assets" / "models" / "rock-2.glb");
  EXPECT_EQ(importDestination(m_dir, "x/rock.glb", "other", ImportKind::model).relative, "assets/models/other.glb");
}

TEST_F(ProjectPathsTest, ImportDestinationWithNoRootIsRelativeAndOverwrites)
{
  const auto destination = importDestination({}, "x/rock.glb", "rock", ImportKind::model);

  EXPECT_EQ(destination.relative, "assets/models/rock.glb");
  EXPECT_EQ(destination.absolute, fs::path("assets/models/rock.glb"));
}

TEST_F(ProjectPathsTest, ImportDestinationWithNoRootNeverLooksAtTheWorkingDirectory)
{
  touch("cwd/assets/models/rock.glb");
  const ScopedCurrentPath cwd(m_dir / "cwd");

  const auto destination = importDestination({}, "x/rock.glb", "rock", ImportKind::model);

  EXPECT_EQ(destination.relative, "assets/models/rock.glb");
  EXPECT_EQ(destination.absolute, fs::path("assets/models/rock.glb"));
}

TEST_F(ProjectPathsTest, CopyProjectContentCopiesAssetsAndScriptsButKeepsExistingFiles)
{
  touch("a/assets/models/m.glb");
  touch("a/scripts/S.cs");
  touch("a/project.ecs3d");
  touch("b/assets/models/m.glb");
  { std::ofstream(m_dir / "b/assets/models/m.glb") << "keep"; }

  std::error_code error;
  copyProjectContent(m_dir / "a", m_dir / "b", error);

  ASSERT_FALSE(error);
  EXPECT_TRUE(fs::exists(m_dir / "b/scripts/S.cs"));
  EXPECT_FALSE(fs::exists(m_dir / "b/project.ecs3d"));
  EXPECT_EQ(fs::file_size(m_dir / "b/assets/models/m.glb"), 4u);
}

TEST_F(ProjectPathsTest, CopyProjectContentIgnoresEmptyOrNestedTargets)
{
  touch("a/assets/m.glb");
  std::error_code error;

  copyProjectContent({}, m_dir / "b", error);
  EXPECT_FALSE(fs::exists(m_dir / "b"));

  copyProjectContent(m_dir / "a", m_dir / "a" / "assets" / "inner", error);
  EXPECT_FALSE(error);
  EXPECT_FALSE(fs::exists(m_dir / "a/assets/inner"));

  copyProjectContent(m_dir / "a", m_dir / "a", error);
  EXPECT_FALSE(error);
  EXPECT_FALSE(fs::exists(m_dir / "a/assets/assets"));

  copyProjectContent(m_dir / "a", m_dir / "c", error);
  EXPECT_FALSE(error);
  EXPECT_TRUE(fs::exists(m_dir / "c/assets/m.glb"));
}

TEST_F(ProjectPathsTest, CopyProjectContentWritesNothingForAnEmptyTarget)
{
  touch("a/assets/m.glb");
  const ScopedCurrentPath cwd(m_dir / "cwd");
  std::error_code error;

  copyProjectContent(m_dir / "a", {}, error);

  EXPECT_FALSE(error);
  EXPECT_TRUE(fs::is_empty(m_dir / "cwd"));

  // Positive control: a real target receives the same content.
  copyProjectContent(m_dir / "a", m_dir / "c", error);
  EXPECT_TRUE(fs::exists(m_dir / "c/assets/m.glb"));
}

TEST(PathFromUtf8Test, KeepsNonAsciiCharacters)
{
  const char* utf8 = "proj/caf\xC3\xA9/\xE3\x82\xB2\xE3\x83\xBC\xE3\x83\xA0.ecs3d";
  const auto path = pathFromUtf8(utf8);

  EXPECT_EQ(pathToUtf8(path.filename()), "\xE3\x82\xB2\xE3\x83\xBC\xE3\x83\xA0.ecs3d");
  EXPECT_EQ(pathToUtf8(path), std::string(utf8));
  EXPECT_EQ(pathToUtf8(pathFromUtf8("plain/path")), "plain/path");
  EXPECT_TRUE(pathFromUtf8(nullptr).empty());
}

TEST_F(ProjectPathsTest, SaveFolderHoldsOtherProjectOnlyForADifferentFolderWithAProject)
{
  touch("a/project.ecs3d");
  fs::create_directories(m_dir / "empty");

  EXPECT_TRUE(saveFolderHoldsOtherProject(m_dir / "a", {}));
  EXPECT_TRUE(saveFolderHoldsOtherProject(m_dir / "a", m_dir / "empty"));
  EXPECT_FALSE(saveFolderHoldsOtherProject(m_dir / "a", m_dir / "a"));
  EXPECT_FALSE(saveFolderHoldsOtherProject(m_dir / "empty", m_dir / "a"));
  EXPECT_FALSE(saveFolderHoldsOtherProject(m_dir / "missing", {}));
}

TEST_F(ProjectPathsTest, ImportDestinationKeepsATypedNameInsideAssets)
{
  const auto inside = [this](const ImportDestination& destination) {
    return destination.absolute.parent_path().lexically_normal() ==
           (m_dir / "assets" / "models").lexically_normal();
  };

  EXPECT_TRUE(inside(importDestination(m_dir, "x/rock.glb", "..\\x", ImportKind::model)));
  EXPECT_TRUE(inside(importDestination(m_dir, "x/rock.glb", "/abs/evil", ImportKind::model)));
  EXPECT_TRUE(inside(importDestination(m_dir, "x/rock.glb", "../../up", ImportKind::model)));
  EXPECT_TRUE(inside(importDestination(m_dir, "x/rock.glb", "..", ImportKind::model)));
  EXPECT_EQ(importDestination(m_dir, "x/rock.glb", "/abs/evil", ImportKind::model).relative,
            "assets/models/evil.glb");
  EXPECT_EQ(importDestination(m_dir, "x/rock.glb", "..", ImportKind::model).relative, "assets/models/rock.glb");
}
