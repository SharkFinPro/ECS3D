#include <gtest/gtest.h>

#include "RecentProjectRecording.h"
#include "RecentProjects.h"

#include <chrono>
#include <filesystem>
#include <random>
#include <string>

namespace {
  class RecentProjectRecordingTest : public testing::Test {
  protected:
    void SetUp() override
    {
      std::random_device device;
      m_directory = std::filesystem::temp_directory_path() / ("ecs3d-recording-" + std::to_string(device()));
      m_file = m_directory / "recent-projects.json";
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

TEST_F(RecentProjectRecordingTest, UntitledTemplateRecordsNothingButTheSamePathOtherwiseDoes)
{
  const auto path = m_directory / "Courtyard" / "project.ecs3d";

  EXPECT_FALSE(recentProjectToRecord(path, true).has_value());

  const auto recorded = recentProjectToRecord(path, false);
  ASSERT_TRUE(recorded.has_value());
  EXPECT_EQ(*recorded, path);
}

TEST_F(RecentProjectRecordingTest, ProjectWithNoFileYetRecordsNothing)
{
  EXPECT_FALSE(recentProjectToRecord({}, false).has_value());
  EXPECT_FALSE(recentProjectToRecord({}, true).has_value());
  EXPECT_TRUE(recentProjectToRecord(m_directory / "Game.json", false).has_value());
}

TEST_F(RecentProjectRecordingTest, LegacyJsonIsRecordedAsTheFile)
{
  const auto path = m_directory / "Legacy" / "Game.json";

  const auto recorded = recentProjectToRecord(path, false);
  ASSERT_TRUE(recorded.has_value());
  EXPECT_EQ(*recorded, path);
}

TEST_F(RecentProjectRecordingTest, RecordedPathLandsAtTheFrontOfTheList)
{
  const auto now = std::chrono::system_clock::now();
  RecentProjects recent(m_file);
  recent.recordOpened(m_directory / "Older", now);

  const auto recorded = recentProjectToRecord(m_directory / "Courtyard" / "project.ecs3d", false);
  ASSERT_TRUE(recorded.has_value());
  recent.recordOpened(*recorded, now + std::chrono::seconds(1));
  ASSERT_TRUE(recent.save());

  RecentProjects reloaded(m_file);
  reloaded.load();
  const auto entries = reloaded.entries();
  ASSERT_EQ(entries.size(), 2u);
  EXPECT_EQ(entries[0].name, "Courtyard");
  EXPECT_EQ(entries[1].name, "Older");
}
