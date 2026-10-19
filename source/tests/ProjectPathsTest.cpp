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
