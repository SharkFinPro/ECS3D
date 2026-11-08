#include <gtest/gtest.h>

#include "FramePacingSettings.h"
#include "SettingsStore.h"

#include <climits>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

namespace {
  class FramePacingSettings : public testing::Test {
  protected:
    void SetUp() override
    {
      const auto* info = testing::UnitTest::GetInstance()->current_test_info();

      m_directory = std::filesystem::temp_directory_path() /
                    ("ecs3d-" + std::string(info->test_suite_name()) + "-" + std::string(info->name()));

      std::error_code error;
      std::filesystem::remove_all(m_directory, error);
      std::filesystem::create_directories(m_directory, error);

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

  const framePacingSettings::Keys* const allKeys[] = {
    &framePacingSettings::clientKeys,
    &framePacingSettings::editorKeys
  };
}

TEST_F(FramePacingSettings, DefaultsToVsyncOnAndUnlimited)
{
  const SettingsStore settings(m_file);

  EXPECT_TRUE(framePacingSettings::defaultVsync);
  EXPECT_EQ(framePacingSettings::defaultMaxFps, framePacingSettings::unlimited);

  for (const auto* keys : allKeys)
  {
    EXPECT_TRUE(framePacingSettings::readVsync(settings, *keys));
    EXPECT_EQ(framePacingSettings::readMaxFps(settings, *keys), framePacingSettings::unlimited);
  }
}

TEST_F(FramePacingSettings, KeysArePersistedNames)
{
  EXPECT_STREQ(framePacingSettings::clientKeys.vsync, "client.display.vsync");
  EXPECT_STREQ(framePacingSettings::clientKeys.maxFps, "client.display.maxFps");
  EXPECT_STREQ(framePacingSettings::editorKeys.vsync, "editor.display.vsync");
  EXPECT_STREQ(framePacingSettings::editorKeys.maxFps, "editor.display.maxFps");
}

TEST_F(FramePacingSettings, ValuesSurviveAReload)
{
  for (const auto* keys : allKeys)
  {
    {
      SettingsStore settings(m_file);
      framePacingSettings::writeVsync(settings, *keys, false);
      framePacingSettings::writeMaxFps(settings, *keys, 144);
      settings.flush();
    }

    const SettingsStore reloaded(m_file);

    EXPECT_FALSE(framePacingSettings::readVsync(reloaded, *keys));
    EXPECT_EQ(framePacingSettings::readMaxFps(reloaded, *keys), 144);
  }
}

TEST_F(FramePacingSettings, ClientAndEditorKeysAreIndependent)
{
  SettingsStore settings(m_file);

  framePacingSettings::writeVsync(settings, framePacingSettings::clientKeys, false);
  framePacingSettings::writeMaxFps(settings, framePacingSettings::clientKeys, 60);

  EXPECT_FALSE(framePacingSettings::readVsync(settings, framePacingSettings::clientKeys));
  EXPECT_EQ(framePacingSettings::readMaxFps(settings, framePacingSettings::clientKeys), 60);

  EXPECT_TRUE(framePacingSettings::readVsync(settings, framePacingSettings::editorKeys));
  EXPECT_EQ(framePacingSettings::readMaxFps(settings, framePacingSettings::editorKeys),
            framePacingSettings::unlimited);

  framePacingSettings::writeVsync(settings, framePacingSettings::editorKeys, false);
  framePacingSettings::writeMaxFps(settings, framePacingSettings::editorKeys, 90);

  EXPECT_EQ(framePacingSettings::readMaxFps(settings, framePacingSettings::editorKeys), 90);
  EXPECT_EQ(framePacingSettings::readMaxFps(settings, framePacingSettings::clientKeys), 60);
}

TEST_F(FramePacingSettings, ClampMaxFpsBoundaries)
{
  using framePacingSettings::clampMaxFps;

  EXPECT_EQ(clampMaxFps(INT_MIN), 0);
  EXPECT_EQ(clampMaxFps(-1), 0);
  EXPECT_EQ(clampMaxFps(0), 0);
  EXPECT_EQ(clampMaxFps(1), 10);
  EXPECT_EQ(clampMaxFps(9), 10);
  EXPECT_EQ(clampMaxFps(10), 10);
  EXPECT_EQ(clampMaxFps(11), 11);
  EXPECT_EQ(clampMaxFps(999), 999);
  EXPECT_EQ(clampMaxFps(1000), 1000);
  EXPECT_EQ(clampMaxFps(1001), 1000);
  EXPECT_EQ(clampMaxFps(INT_MAX), 1000);
}

TEST_F(FramePacingSettings, WriteMaxFpsClampsWhatItStores)
{
  SettingsStore settings(m_file);
  const auto& keys = framePacingSettings::clientKeys;

  framePacingSettings::writeMaxFps(settings, keys, 5);
  EXPECT_EQ(settings.get<int>(keys.maxFps, -42), 10);

  framePacingSettings::writeMaxFps(settings, keys, 100000);
  EXPECT_EQ(settings.get<int>(keys.maxFps, -42), 1000);

  framePacingSettings::writeMaxFps(settings, keys, -7);
  EXPECT_EQ(settings.get<int>(keys.maxFps, -42), 0);

  framePacingSettings::writeMaxFps(settings, keys, 75);
  EXPECT_EQ(settings.get<int>(keys.maxFps, -42), 75);
}

TEST_F(FramePacingSettings, ReadMaxFpsClampsHandWrittenValues)
{
  SettingsStore settings(m_file);
  const auto& keys = framePacingSettings::editorKeys;

  settings.set(keys.maxFps, 5);
  EXPECT_EQ(framePacingSettings::readMaxFps(settings, keys), 10);

  settings.set(keys.maxFps, 99999);
  EXPECT_EQ(framePacingSettings::readMaxFps(settings, keys), 1000);

  settings.set(keys.maxFps, 99999999999LL);
  EXPECT_EQ(framePacingSettings::readMaxFps(settings, keys), 1000);

  settings.set(keys.maxFps, -30);
  EXPECT_EQ(framePacingSettings::readMaxFps(settings, keys), 0);

  settings.set(keys.maxFps, 59.5);
  EXPECT_EQ(framePacingSettings::readMaxFps(settings, keys), 59);

  settings.set(keys.maxFps, 1.0e30);
  EXPECT_EQ(framePacingSettings::readMaxFps(settings, keys), 1000);

  settings.set(keys.maxFps, -1.0e30);
  EXPECT_EQ(framePacingSettings::readMaxFps(settings, keys), 0);

  settings.set(keys.maxFps, 120);
  EXPECT_EQ(framePacingSettings::readMaxFps(settings, keys), 120);
}

TEST_F(FramePacingSettings, WrongTypesFallBackToTheDefaults)
{
  SettingsStore settings(m_file);
  const auto& keys = framePacingSettings::clientKeys;

  settings.set(keys.maxFps, std::string("fast"));
  settings.set(keys.vsync, std::string("off"));

  EXPECT_EQ(framePacingSettings::readMaxFps(settings, keys), framePacingSettings::unlimited);
  EXPECT_TRUE(framePacingSettings::readVsync(settings, keys));

  settings.set(keys.maxFps, nlohmann::json::object());
  settings.set(keys.vsync, nlohmann::json::object());

  EXPECT_EQ(framePacingSettings::readMaxFps(settings, keys), framePacingSettings::unlimited);
  EXPECT_TRUE(framePacingSettings::readVsync(settings, keys));

  settings.set(keys.maxFps, 60);
  settings.set(keys.vsync, false);

  EXPECT_EQ(framePacingSettings::readMaxFps(settings, keys), 60);
  EXPECT_FALSE(framePacingSettings::readVsync(settings, keys));
}

TEST_F(FramePacingSettings, AFileThatIsNotJsonYieldsDefaults)
{
  {
    std::ofstream out(m_file);
    out << "this is { not json";
  }

  const SettingsStore settings(m_file);

  for (const auto* keys : allKeys)
  {
    EXPECT_TRUE(framePacingSettings::readVsync(settings, *keys));
    EXPECT_EQ(framePacingSettings::readMaxFps(settings, *keys), framePacingSettings::unlimited);
  }
}

TEST_F(FramePacingSettings, ParseMaxFpsAcceptsWholeNumbers)
{
  using framePacingSettings::parseMaxFps;

  EXPECT_EQ(parseMaxFps("0"), std::optional<int>(0));
  EXPECT_EQ(parseMaxFps("60"), std::optional<int>(60));
  EXPECT_EQ(parseMaxFps("5"), std::optional<int>(10));
  EXPECT_EQ(parseMaxFps("100000"), std::optional<int>(1000));
}

TEST_F(FramePacingSettings, ParseMaxFpsRejectsEverythingElse)
{
  using framePacingSettings::parseMaxFps;

  for (const char* text : { "", "-1", "+60", "60fps", "6.5", " 60", "abc", "99999999999999999999" })
  {
    EXPECT_FALSE(parseMaxFps(text).has_value()) << "'" << text << "'";
  }
}

TEST_F(FramePacingSettings, ResolveVsyncUsesTheStoredValueWithoutAFlag)
{
  SettingsStore settings(m_file);
  const auto& keys = framePacingSettings::clientKeys;

  EXPECT_TRUE(framePacingSettings::resolveVsync(settings, keys, std::nullopt));

  framePacingSettings::writeVsync(settings, keys, false);
  EXPECT_FALSE(framePacingSettings::resolveVsync(settings, keys, std::nullopt));
}

TEST_F(FramePacingSettings, ResolveVsyncFlagOverridesBothDirectionsWithoutWriting)
{
  SettingsStore settings(m_file);
  const auto& keys = framePacingSettings::clientKeys;

  framePacingSettings::writeVsync(settings, keys, false);
  settings.flush();
  ASSERT_FALSE(settings.hasPendingWrite());

  EXPECT_TRUE(framePacingSettings::resolveVsync(settings, keys, true));
  EXPECT_FALSE(settings.hasPendingWrite());
  EXPECT_FALSE(framePacingSettings::readVsync(settings, keys));

  framePacingSettings::writeVsync(settings, keys, true);
  settings.flush();

  EXPECT_FALSE(framePacingSettings::resolveVsync(settings, keys, false));
  EXPECT_FALSE(settings.hasPendingWrite());
  EXPECT_TRUE(framePacingSettings::readVsync(settings, keys));
}

TEST_F(FramePacingSettings, ResolveMaxFpsFlagOverridesWithoutWriting)
{
  SettingsStore settings(m_file);
  const auto& keys = framePacingSettings::editorKeys;

  EXPECT_EQ(framePacingSettings::resolveMaxFps(settings, keys, std::nullopt), framePacingSettings::unlimited);

  framePacingSettings::writeMaxFps(settings, keys, 144);
  settings.flush();
  ASSERT_FALSE(settings.hasPendingWrite());

  EXPECT_EQ(framePacingSettings::resolveMaxFps(settings, keys, std::nullopt), 144);
  EXPECT_EQ(framePacingSettings::resolveMaxFps(settings, keys, 30), 30);
  EXPECT_EQ(framePacingSettings::resolveMaxFps(settings, keys, 0), framePacingSettings::unlimited);
  EXPECT_EQ(framePacingSettings::resolveMaxFps(settings, keys, 5), 10);
  EXPECT_EQ(framePacingSettings::resolveMaxFps(settings, keys, 100000), 1000);

  EXPECT_FALSE(settings.hasPendingWrite());
  EXPECT_EQ(framePacingSettings::readMaxFps(settings, keys), 144);
}
