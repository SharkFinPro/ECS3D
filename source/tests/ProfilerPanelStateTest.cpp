#include <gtest/gtest.h>

#include <ProfilerPanelState.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {
  class ProfilerPanelStateFixture : public testing::Test {
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

    void writeFile(const std::string& contents) const
    {
      std::filesystem::create_directories(m_directory);
      std::ofstream out(m_file, std::ios::trunc);
      out << contents;
    }

    std::filesystem::path m_directory;
    std::filesystem::path m_file;
  };

  using ProfilerPanelState = ProfilerPanelStateFixture;

  uint32_t maskOf(const std::string& id)
  {
    const auto groups = profile::displayGroups(profile::LaneKind::frame);

    for (std::size_t i = 0; i < groups.size(); ++i)
    {
      if (groups[i].id == id)
      {
        return 1u << i;
      }
    }

    return 0;
  }
}

TEST_F(ProfilerPanelState, DefaultsApplyWhenNothingIsStored)
{
  const SettingsStore settings(m_file);

  EXPECT_EQ(profilerPanelState::readWindowSeconds(settings), 5);
  EXPECT_EQ(profilerPanelState::readScaleMillis(settings), profilerPanelState::autoScale);
  EXPECT_EQ(profilerPanelState::readHiddenMask(settings), 0u);
}

TEST_F(ProfilerPanelState, ChoicesRoundTripThroughAReloadedStore)
{
  {
    SettingsStore settings(m_file);
    profilerPanelState::writeWindowSeconds(settings, 10);
    profilerPanelState::writeScaleMillis(settings, profile::fixedScalesMillis[2]);
    profilerPanelState::writeHiddenMask(settings, maskOf("renderer") | maskOf("input"));
    settings.flush();
  }

  const SettingsStore reloaded(m_file);

  EXPECT_EQ(profilerPanelState::readWindowSeconds(reloaded), 10);
  EXPECT_DOUBLE_EQ(profilerPanelState::readScaleMillis(reloaded), profile::fixedScalesMillis[2]);
  EXPECT_EQ(profilerPanelState::readHiddenMask(reloaded), maskOf("renderer") | maskOf("input"));
  EXPECT_NE(maskOf("renderer"), 0u);
  EXPECT_NE(maskOf("input"), 0u);
}

TEST_F(ProfilerPanelState, HiddenGroupsAreStoredAsGroupIds)
{
  SettingsStore settings(m_file);
  profilerPanelState::writeHiddenMask(settings, maskOf("editorUi"));

  const auto ids = settings.get<std::vector<std::string>>(profilerPanelState::hiddenGroupsKey, {});

  ASSERT_EQ(ids.size(), 1u);
  EXPECT_EQ(ids[0], "editorUi");
}

TEST_F(ProfilerPanelState, InvalidStoredValuesFallBackToTheDefaults)
{
  writeFile(R"({"editor.profiler.window": 7, "editor.profiler.scale": 12.5,
                "editor.profiler.hiddenGroups": "renderer"})");
  const SettingsStore settings(m_file);

  EXPECT_EQ(profilerPanelState::readWindowSeconds(settings), 5);
  EXPECT_EQ(profilerPanelState::readScaleMillis(settings), profilerPanelState::autoScale);
  EXPECT_EQ(profilerPanelState::readHiddenMask(settings), 0u);
}

TEST_F(ProfilerPanelState, WrongTypedValuesFallBackToTheDefaults)
{
  writeFile(R"({"editor.profiler.window": "ten", "editor.profiler.scale": "fast",
                "editor.profiler.hiddenGroups": [1, 2]})");
  const SettingsStore settings(m_file);

  EXPECT_EQ(profilerPanelState::readWindowSeconds(settings), 5);
  EXPECT_EQ(profilerPanelState::readScaleMillis(settings), profilerPanelState::autoScale);
  EXPECT_EQ(profilerPanelState::readHiddenMask(settings), 0u);
}

TEST_F(ProfilerPanelState, UnknownHiddenGroupIdsAreIgnored)
{
  writeFile(R"({"editor.profiler.hiddenGroups": ["renderer", "scripts", "nonsense"]})");
  const SettingsStore settings(m_file);

  EXPECT_EQ(profilerPanelState::readHiddenMask(settings), maskOf("renderer"));
}

TEST_F(ProfilerPanelState, EveryOfferedChoiceIsAccepted)
{
  SettingsStore settings(m_file);

  for (const int seconds : profilerPanelState::windowChoicesSeconds)
  {
    profilerPanelState::writeWindowSeconds(settings, seconds);
    EXPECT_EQ(profilerPanelState::readWindowSeconds(settings), seconds);
  }

  for (const double millis : profile::fixedScalesMillis)
  {
    profilerPanelState::writeScaleMillis(settings, millis);
    EXPECT_DOUBLE_EQ(profilerPanelState::readScaleMillis(settings), millis);
  }
}

TEST(ProfilerPanelInteraction, ClickingAColumnSelectsItAndPauses)
{
  profilerPanelState::Interaction interaction;

  interaction.clickColumn(42);

  EXPECT_TRUE(interaction.paused);
  EXPECT_EQ(interaction.selectedSequence, 42u);
}

TEST(ProfilerPanelInteraction, ClickingAnotherColumnMovesTheSelection)
{
  profilerPanelState::Interaction interaction;
  interaction.clickColumn(42);

  interaction.clickColumn(43);

  EXPECT_TRUE(interaction.paused);
  EXPECT_EQ(interaction.selectedSequence, 43u);
}

TEST(ProfilerPanelInteraction, ClickingTheSelectedColumnResumes)
{
  profilerPanelState::Interaction interaction;
  interaction.clickColumn(42);

  interaction.clickColumn(42);

  EXPECT_FALSE(interaction.paused);
  EXPECT_EQ(interaction.selectedSequence, 0u);
}

TEST(ProfilerPanelInteraction, ClickingNothingChangesNothing)
{
  profilerPanelState::Interaction interaction;
  interaction.clickColumn(42);

  interaction.clickColumn(0);

  EXPECT_TRUE(interaction.paused);
  EXPECT_EQ(interaction.selectedSequence, 42u);
}

TEST(ProfilerPanelInteraction, ResumeClearsThePauseAndTheSelection)
{
  profilerPanelState::Interaction interaction;
  interaction.clickColumn(42);

  interaction.togglePause();

  EXPECT_FALSE(interaction.paused);
  EXPECT_EQ(interaction.selectedSequence, 0u);
}

TEST(ProfilerPanelInteraction, ClosingDropsThePauseAndTheSelection)
{
  profilerPanelState::Interaction interaction;
  interaction.clickColumn(42);
  ASSERT_TRUE(interaction.paused);

  interaction.close();

  EXPECT_FALSE(interaction.paused);
  EXPECT_EQ(interaction.selectedSequence, 0u);
}

TEST(ProfilerPanelInteraction, PausingWithoutAClickSelectsNothing)
{
  profilerPanelState::Interaction interaction;

  interaction.togglePause();

  EXPECT_TRUE(interaction.paused);
  EXPECT_EQ(interaction.selectedSequence, 0u);

  interaction.togglePause();

  EXPECT_FALSE(interaction.paused);
}

TEST(ProfilerPanelWindow, P99WallIsTakenOverTheNewestWindowOnly)
{
  profile::History history(200);

  for (uint64_t i = 1; i <= 100; ++i)
  {
    profile::Sample sample;
    sample.sequence = i;
    sample.wallMicros = i <= 50 ? 90000u : 1000u;
    history.push(sample);
  }

  EXPECT_EQ(profilerPanelState::p99WallMicros(history, 50), 1000u);
  EXPECT_EQ(profilerPanelState::p99WallMicros(history, 100), 90000u);
  EXPECT_EQ(profilerPanelState::p99WallMicros(profile::History(4), 10), 0u);
}
