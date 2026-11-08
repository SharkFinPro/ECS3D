#include <gtest/gtest.h>

#include "ClientDisplaySettings.h"
#include "SettingsStore.h"

#include <filesystem>
#include <string>
#include <system_error>

namespace {
  class ClientDisplaySettings : public testing::Test {
  protected:
    void SetUp() override
    {
      const auto* info = testing::UnitTest::GetInstance()->current_test_info();

      m_directory = std::filesystem::temp_directory_path() /
                    ("ecs3d-" + std::string(info->test_suite_name()) + "-" + std::string(info->name()));

      std::error_code error;
      std::filesystem::remove_all(m_directory, error);

      m_file = m_directory / "client-settings.json";
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

TEST_F(ClientDisplaySettings, FullscreenDefaultsToWindowed)
{
  const SettingsStore settings(m_file);

  EXPECT_FALSE(clientDisplaySettings::defaultFullscreen);
  EXPECT_FALSE(clientDisplaySettings::readFullscreen(settings));
}

TEST_F(ClientDisplaySettings, FullscreenSurvivesAReload)
{
  {
    SettingsStore settings(m_file);
    clientDisplaySettings::writeFullscreen(settings, true);
    settings.flush();
  }

  const SettingsStore reloaded(m_file);

  EXPECT_TRUE(clientDisplaySettings::readFullscreen(reloaded));
}

TEST_F(ClientDisplaySettings, WindowedOverwritesAStoredFullscreenAcrossAReload)
{
  {
    SettingsStore settings(m_file);
    clientDisplaySettings::writeFullscreen(settings, true);
    settings.flush();
  }

  {
    SettingsStore settings(m_file);
    ASSERT_TRUE(clientDisplaySettings::readFullscreen(settings));
    clientDisplaySettings::writeFullscreen(settings, false);
    settings.flush();
  }

  const SettingsStore reloaded(m_file);

  EXPECT_FALSE(clientDisplaySettings::readFullscreen(reloaded));
}

TEST_F(ClientDisplaySettings, FullscreenIsStoredUnderItsPersistedKey)
{
  SettingsStore settings(m_file);

  clientDisplaySettings::writeFullscreen(settings, true);

  EXPECT_STREQ(clientDisplaySettings::fullscreenKey, "client.display.fullscreen");
  EXPECT_TRUE(settings.get<bool>(clientDisplaySettings::fullscreenKey, false));
}

TEST_F(ClientDisplaySettings, FullscreenOfTheWrongTypeFallsBackToTheDefault)
{
  SettingsStore settings(m_file);

  settings.set(clientDisplaySettings::fullscreenKey, std::string("yes"));
  EXPECT_FALSE(clientDisplaySettings::readFullscreen(settings));

  settings.set(clientDisplaySettings::fullscreenKey, true);
  EXPECT_TRUE(clientDisplaySettings::readFullscreen(settings));
}

TEST_F(ClientDisplaySettings, WindowedFlagForcesWindowedOverAStoredFullscreen)
{
  SettingsStore settings(m_file);
  clientDisplaySettings::writeFullscreen(settings, true);

  EXPECT_FALSE(clientDisplaySettings::startsFullscreen(settings, true));
  EXPECT_TRUE(clientDisplaySettings::readFullscreen(settings));
}

TEST_F(ClientDisplaySettings, WithoutTheWindowedFlagTheStoredValueWins)
{
  SettingsStore settings(m_file);

  clientDisplaySettings::writeFullscreen(settings, true);
  EXPECT_TRUE(clientDisplaySettings::startsFullscreen(settings, false));

  clientDisplaySettings::writeFullscreen(settings, false);
  EXPECT_FALSE(clientDisplaySettings::startsFullscreen(settings, false));
}

TEST(ClientDisplaySettingsFile, SitsBesideTheEditorSettingsUnderItsOwnName)
{
  const auto client = clientDisplaySettings::defaultFile();
  const auto editor = SettingsStore::defaultFile();

  EXPECT_EQ(client.filename(), "client-settings.json");
  EXPECT_EQ(client.parent_path(), editor.parent_path());
  EXPECT_NE(client, editor);
}
