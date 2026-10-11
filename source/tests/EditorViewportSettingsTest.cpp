#include <gtest/gtest.h>

#include "EditorViewportSettings.h"
#include "SettingsStore.h"

#include <filesystem>
#include <string>
#include <system_error>

namespace {
  class EditorViewportSettings : public testing::Test {
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

TEST_F(EditorViewportSettings, ShowGridDefaultsToTrue)
{
  const SettingsStore settings(m_file);

  EXPECT_TRUE(editorViewportSettings::defaultShowGrid);
  EXPECT_TRUE(editorViewportSettings::readShowGrid(settings));
}

TEST_F(EditorViewportSettings, ShowGridRoundTripsBothValues)
{
  SettingsStore settings(m_file);

  editorViewportSettings::writeShowGrid(settings, false);
  EXPECT_FALSE(editorViewportSettings::readShowGrid(settings));

  editorViewportSettings::writeShowGrid(settings, true);
  EXPECT_TRUE(editorViewportSettings::readShowGrid(settings));
}
