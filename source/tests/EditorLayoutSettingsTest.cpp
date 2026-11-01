#include <gtest/gtest.h>

#include "EditorLayoutSettings.h"
#include "SettingsStore.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace {
  const std::string savedLayout =
    "[Window][Console]\n"
    "ViewportPos=1688,484\n"
    "ViewportId=0xF7BA560E\n"
    "Size=883,250\n"
    "Collapsed=0\n"
    "\n"
    "[Docking][Data]\n"
    "DockSpace         ID=0x8D4D86C3 Window=0x1BBC0F80 Pos=41,102 Size=1600,868 Split=X\n";

  const std::string windowOnlyLayout =
    "[Window][Console]\n"
    "Pos=10,10\n"
    "Size=883,250\n"
    "Collapsed=0\n";

  class EditorLayoutSettings : public testing::Test {
  protected:
    void SetUp() override
    {
      const auto* info = testing::UnitTest::GetInstance()->current_test_info();

      m_directory = std::filesystem::temp_directory_path() /
                    ("ecs3d-" + std::string(info->test_suite_name()) + "-" + std::string(info->name()));

      std::error_code error;
      std::filesystem::remove_all(m_directory, error);

      m_file = m_directory / "settings.json";
    }

    void TearDown() override
    {
      std::error_code error;
      std::filesystem::remove_all(m_directory, error);
    }

    std::filesystem::path m_directory;
    std::filesystem::path m_file;
  };
}

TEST_F(EditorLayoutSettings, LayoutDefaultsToEmpty)
{
  const SettingsStore settings(m_file);

  EXPECT_TRUE(editorLayoutSettings::readImGuiLayout(settings).empty());
}

TEST_F(EditorLayoutSettings, LayoutRoundTripsMultiLineText)
{
  SettingsStore settings(m_file);

  editorLayoutSettings::writeImGuiLayout(settings, savedLayout);

  EXPECT_EQ(editorLayoutSettings::readImGuiLayout(settings), savedLayout);
}

TEST_F(EditorLayoutSettings, LayoutSurvivesReloadFromTheSameFile)
{
  {
    SettingsStore settings(m_file);
    editorLayoutSettings::writeImGuiLayout(settings, savedLayout);
    settings.flush();
  }

  const SettingsStore reloaded(m_file);

  EXPECT_EQ(editorLayoutSettings::readImGuiLayout(reloaded), savedLayout);
}

TEST_F(EditorLayoutSettings, LayoutIsStoredUnderItsPersistedKey)
{
  std::filesystem::create_directories(m_directory);
  {
    std::ofstream out(m_file);
    out << R"({"editor.layout.imgui": "[Docking][Data]\nDockSpace ID=0x1\n"})";
  }

  const SettingsStore settings(m_file);

  EXPECT_EQ(editorLayoutSettings::readImGuiLayout(settings), "[Docking][Data]\nDockSpace ID=0x1\n");
}

TEST_F(EditorLayoutSettings, WrongTypedLayoutReadsAsEmpty)
{
  std::filesystem::create_directories(m_directory);
  {
    std::ofstream out(m_file);
    out << R"({"editor.layout.imgui": 5})";
  }

  const SettingsStore settings(m_file);

  EXPECT_TRUE(editorLayoutSettings::readImGuiLayout(settings).empty());
}

TEST(EditorLayoutDockDetection, SavedLayoutWithDockSpaceHasADockLayout)
{
  EXPECT_TRUE(editorLayoutSettings::hasDockLayout(savedLayout));
}

TEST(EditorLayoutDockDetection, CrlfLineEndingsAreAccepted)
{
  const std::string crlf = "[Window][A]\r\nSize=1,1\r\n\r\n[Docking][Data]\r\nDockSpace ID=0x1\r\n";

  EXPECT_TRUE(editorLayoutSettings::hasDockLayout(crlf));
}

TEST(EditorLayoutDockDetection, EmptyTextHasNoDockLayout)
{
  EXPECT_FALSE(editorLayoutSettings::hasDockLayout(""));
  EXPECT_TRUE(editorLayoutSettings::hasDockLayout(savedLayout));
}

TEST(EditorLayoutDockDetection, GarbageHasNoDockLayout)
{
  EXPECT_FALSE(editorLayoutSettings::hasDockLayout("not an ini file\n\x01\x02 DockSpace"));
  EXPECT_TRUE(editorLayoutSettings::hasDockLayout(savedLayout));
}

TEST(EditorLayoutDockDetection, WindowOnlyLayoutHasNoDockLayout)
{
  EXPECT_FALSE(editorLayoutSettings::hasDockLayout(windowOnlyLayout));
  EXPECT_TRUE(editorLayoutSettings::hasDockLayout(windowOnlyLayout + savedLayout));
}

TEST(EditorLayoutDockDetection, EmptyDockSectionHasNoDockLayout)
{
  EXPECT_FALSE(editorLayoutSettings::hasDockLayout("[Docking][Data]\n"));
  EXPECT_FALSE(editorLayoutSettings::hasDockLayout("[Docking][Data]\n\n[Window][A]\nSize=1,1\n"));
}

TEST(EditorLayoutDockDetection, DockSpaceOutsideTheDockSectionDoesNotCount)
{
  EXPECT_FALSE(editorLayoutSettings::hasDockLayout("[Window][A]\nDockSpace ID=0x1\n"));
  EXPECT_TRUE(editorLayoutSettings::hasDockLayout("[Window][A]\nSize=1,1\n[Docking][Data]\nDockSpace ID=0x1\n"));
}

TEST(EditorLayoutReplacement, TextWithoutADockTreeNeverReplacesOneWithIt)
{
  EXPECT_FALSE(editorLayoutSettings::shouldReplaceLayout(savedLayout, windowOnlyLayout));
  EXPECT_FALSE(editorLayoutSettings::shouldReplaceLayout(savedLayout, ""));
  EXPECT_TRUE(editorLayoutSettings::shouldReplaceLayout(savedLayout, savedLayout + "x"));
}

TEST(EditorLayoutReplacement, AnythingIsWrittenOverAStoredLayoutWithoutADockTree)
{
  EXPECT_TRUE(editorLayoutSettings::shouldReplaceLayout("", windowOnlyLayout));
  EXPECT_TRUE(editorLayoutSettings::shouldReplaceLayout(windowOnlyLayout, windowOnlyLayout));
  EXPECT_TRUE(editorLayoutSettings::shouldReplaceLayout("", savedLayout));
}
