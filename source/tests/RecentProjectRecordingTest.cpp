#include <gtest/gtest.h>

#include "RecentProjectRecording.h"
#include "RecentProjects.h"

#include <chrono>
#include <filesystem>
#include <fstream>
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

    // A project folder holding project.ecs3d, plus template.json when asTemplate.
    [[nodiscard]] ProjectLocation makeProject(const std::string& name, const bool asTemplate) const
    {
      const auto folder = m_directory / name;
      std::filesystem::create_directories(folder);
      std::ofstream(folder / "project.ecs3d") << "{}";
      if (asTemplate)
      {
        std::ofstream(folder / "template.json") << "{}";
      }

      return locateProject(folder);
    }

    std::filesystem::path m_directory;
    std::filesystem::path m_file;
  };
}

TEST_F(RecentProjectRecordingTest, TemplateFolderIsNotRecordedButTheSameFolderWithoutTemplateJsonIs)
{
  const auto plain = makeProject("Plain", false);
  const auto recorded = recentProjectToRecord(plain, false);
  ASSERT_TRUE(recorded.has_value());
  EXPECT_EQ(*recorded, plain.projectFile);

  EXPECT_FALSE(recentProjectToRecord(makeProject("Template", true), false).has_value());
}

TEST_F(RecentProjectRecordingTest, TemplateIsRecordedOnceSavedToAFolder)
{
  const auto tpl = makeProject("Template", true);

  EXPECT_FALSE(recentProjectToRecord(tpl, false).has_value());
  EXPECT_TRUE(recentProjectToRecord(tpl, true).has_value());
}

TEST_F(RecentProjectRecordingTest, BuiltInDefaultIsNotRecorded)
{
  EXPECT_FALSE(recentProjectToRecord(locateProject({}), false).has_value());
  EXPECT_TRUE(recentProjectToRecord(makeProject("Plain", false), false).has_value());
}

TEST_F(RecentProjectRecordingTest, LegacyJsonIsRecordedAsTheFile)
{
  std::filesystem::create_directories(m_directory);
  const auto legacy = m_directory / "Game.json";
  std::ofstream(legacy) << "{}";

  const auto recorded = recentProjectToRecord(locateProject(legacy), false);
  ASSERT_TRUE(recorded.has_value());
  EXPECT_EQ(*recorded, legacy);
}

TEST_F(RecentProjectRecordingTest, RecordedPathLandsAtTheFrontOfTheList)
{
  const auto now = std::chrono::system_clock::now();
  RecentProjects recent(m_file);
  recent.recordOpened(m_directory / "Older", now);

  const auto recorded = recentProjectToRecord(makeProject("Courtyard", false), false);
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
