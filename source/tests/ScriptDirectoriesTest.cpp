#include "ScriptSystemTestFixtures.h"
#include "ScriptDirectories.h"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace {
  using namespace scriptSystemFixtures;

  TEST(ScriptDirectoriesTest, AnEmptyProjectRootLeavesOnlyTheBuiltInDirectory)
  {
    const auto directories = scripting::scriptDirectories("builtin", {});

    ASSERT_EQ(directories.size(), 1u);
    EXPECT_EQ(directories[0], std::filesystem::path("builtin"));
  }

  TEST(ScriptDirectoriesTest, AProjectRootAddsItsScriptsFolderAfterTheBuiltInDirectory)
  {
    const std::filesystem::path root = std::filesystem::path("games") / "demo";

    const auto directories = scripting::scriptDirectories("builtin", root);

    ASSERT_EQ(directories.size(), 2u);
    EXPECT_EQ(directories[0], std::filesystem::path("builtin"));
    EXPECT_EQ(directories[1], root / "scripts");
  }

  TEST(ScriptDirectoriesTest, TheBridgeFormJoinsWithNewlinesInOrder)
  {
    EXPECT_EQ(scripting::joinScriptDirectories({}), "");
    EXPECT_EQ(scripting::joinScriptDirectories({ "a" }), "a");
    EXPECT_EQ(scripting::joinScriptDirectories({ "a", "b", "c" }), "a\nb\nc");
  }

  class ScriptSystemProjectScriptsTest : public ScriptSystemTest {
  protected:
    std::filesystem::path root;
    std::filesystem::path builtIn;
    std::filesystem::path projectScripts;
    ScriptSystem reloading{ factory(), makeDirectories() };

    ~ScriptSystemProjectScriptsTest() override
    {
      std::error_code ec;
      std::filesystem::remove_all(root, ec);
    }

    static void writeScript(const std::filesystem::path& dir, const std::string& name)
    {
      std::filesystem::create_directories(dir);
      std::ofstream file(dir / (name + ".cs"));
      file << "class " << name << " {}";
    }

  private:
    std::vector<std::filesystem::path> makeDirectories()
    {
      const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
      root = std::filesystem::temp_directory_path() / (std::string("ecs3d_project_scripts_") + info->name());
      std::error_code ec;
      std::filesystem::remove_all(root, ec);
      builtIn = root / "builtin";
      std::filesystem::create_directories(builtIn);
      projectScripts = root / "project" / "scripts";

      return scripting::scriptDirectories(builtIn, root / "project");
    }
  };

  TEST_F(ScriptSystemProjectScriptsTest, AFileAddedToTheProjectScriptsReloadsEvenThoughItsFolderDidNotExist)
  {
    writeScript(builtIn, "Existing");
    addScriptedObject("A", { "Mover" });
    reloading.start(manager());
    reloading.fixedUpdate(manager(), 0.6f);
    ASSERT_EQ(count("reload"), 0);

    writeScript(projectScripts, "Added");
    reloading.fixedUpdate(manager(), 0.6f);

    EXPECT_EQ(count("reload"), 1);
  }

  TEST_F(ScriptSystemProjectScriptsTest, AnEditInsideANestedProjectScriptsFolderReloads)
  {
    writeScript(projectScripts / "nested", "Deep");
    addScriptedObject("A", { "Mover" });
    reloading.start(manager());
    reloading.fixedUpdate(manager(), 0.6f);
    ASSERT_EQ(count("reload"), 0);

    writeScript(projectScripts / "nested", "Deeper");
    reloading.fixedUpdate(manager(), 0.6f);

    EXPECT_EQ(count("reload"), 1);
  }
}
