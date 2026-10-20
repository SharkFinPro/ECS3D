#include <gtest/gtest.h>

#include "RecentProjects.h"

#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <regex>
#include <string>

namespace {
  using Clock = std::chrono::system_clock;

  Clock::time_point at(const std::int64_t epochSeconds)
  {
    return Clock::time_point(std::chrono::duration_cast<Clock::duration>(std::chrono::seconds(epochSeconds)));
  }

  constexpr std::int64_t baseTime = 1792350131;  // 2026-10-18T19:02:11Z

  class RecentProjectsTest : public testing::Test {
  protected:
    void SetUp() override
    {
      std::random_device device;
      m_directory = std::filesystem::temp_directory_path() / ("ecs3d-recent-" + std::to_string(device()));
      m_file = m_directory / "recent-projects.json";
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

    [[nodiscard]] nlohmann::json readFile() const
    {
      std::ifstream in(m_file);
      return nlohmann::json::parse(in);
    }

    [[nodiscard]] static std::string generic(const std::filesystem::path& path)
    {
      return path.lexically_normal().generic_string();
    }

    [[nodiscard]] std::filesystem::path project(const std::string& name) const
    {
      return m_directory / name;
    }

    std::filesystem::path m_directory;
    std::filesystem::path m_file;
  };
}

TEST_F(RecentProjectsTest, MissingFileIsEmpty)
{
  RecentProjects recent(m_file);

  EXPECT_NO_THROW(recent.load());
  EXPECT_TRUE(recent.entries().empty());

  recent.recordOpened(project("A"), at(baseTime));
  EXPECT_EQ(recent.entries().size(), 1u);
}

TEST_F(RecentProjectsTest, SaveThenLoadRoundTripsEveryField)
{
  writeFile(R"({"version":1,"projects":[{"path":")" + generic(project("Courtyard"))
            + R"(","name":"Courtyard","lastOpenedUtc":"2026-10-18T19:02:11Z","pinned":true,)"
              R"("template":"first-person"}]})");

  RecentProjects first(m_file);
  first.load();
  first.recordOpened(project("Other"), at(baseTime + 60));
  ASSERT_TRUE(first.save());

  RecentProjects second(m_file);
  second.load();
  const auto entries = second.entries();

  ASSERT_EQ(entries.size(), 2u);
  EXPECT_EQ(entries[0].path, generic(project("Other")));
  EXPECT_EQ(entries[0].name, "Other");
  EXPECT_EQ(entries[0].lastOpenedUtc, "2026-10-18T19:03:11Z");
  EXPECT_FALSE(entries[0].pinned);
  EXPECT_EQ(entries[1].path, generic(project("Courtyard")));
  EXPECT_EQ(entries[1].name, "Courtyard");
  EXPECT_EQ(entries[1].lastOpenedUtc, "2026-10-18T19:02:11Z");
  EXPECT_TRUE(entries[1].pinned);
  EXPECT_EQ(entries[1].templateId, "first-person");
  EXPECT_EQ(readFile().at("version"), 1);
  EXPECT_FALSE(std::filesystem::exists(m_directory / "recent-projects.json.tmp"));
}

TEST_F(RecentProjectsTest, ReopeningMovesToFrontKeepingPinnedTemplateAndUnknownFields)
{
  writeFile(R"({"version":1,"projects":[)"
            R"({"path":")" + generic(project("First")) + R"(","name":"First",)"
            R"("lastOpenedUtc":"2026-10-18T19:02:11Z"},)"
            R"({"path":")" + generic(project("Second")) + R"(","name":"stale",)"
            R"("lastOpenedUtc":"2026-10-17T19:02:11Z","pinned":true,"template":"empty-3d","custom":{"a":1}}]})");

  RecentProjects recent(m_file);
  recent.load();
  recent.recordOpened(project("Second"), at(baseTime + 3600));
  ASSERT_TRUE(recent.save());

  const auto entries = recent.entries();
  ASSERT_EQ(entries.size(), 2u);
  EXPECT_EQ(entries[0].path, generic(project("Second")));
  EXPECT_EQ(entries[0].name, "Second");
  EXPECT_EQ(entries[0].lastOpenedUtc, "2026-10-18T20:02:11Z");
  EXPECT_TRUE(entries[0].pinned);
  EXPECT_EQ(entries[0].templateId, "empty-3d");
  EXPECT_EQ(entries[1].path, generic(project("First")));

  const auto saved = readFile();
  EXPECT_EQ(saved.at("projects").at(0).at("custom"), nlohmann::json({ { "a", 1 } }));
}

TEST_F(RecentProjectsTest, TrailingSlashIsTheSamePath)
{
  RecentProjects recent(m_file);
  recent.recordOpened(project("Game"), at(baseTime));
  recent.recordOpened(project("Game") / "", at(baseTime + 1));

  ASSERT_EQ(recent.entries().size(), 1u);
  EXPECT_EQ(recent.entries()[0].path, generic(project("Game")));
  EXPECT_NE(recent.entries()[0].path.back(), '/');

  recent.recordOpened(project("Elsewhere"), at(baseTime + 2));
  EXPECT_EQ(recent.entries().size(), 2u);
}

#if defined(_WIN32)
TEST_F(RecentProjectsTest, BackslashesAreTheSamePathOnWindows)
{
  RecentProjects recent(m_file);
  recent.recordOpened(std::filesystem::path(generic(project("Game"))), at(baseTime));
  recent.recordOpened(std::filesystem::path(project("Game").make_preferred().native()), at(baseTime + 1));

  ASSERT_EQ(recent.entries().size(), 1u);
  EXPECT_EQ(recent.entries()[0].path.find('\\'),std::string::npos);

  recent.recordOpened(project("Other"), at(baseTime + 2));
  EXPECT_EQ(recent.entries().size(), 2u);
}
#endif

TEST_F(RecentProjectsTest, PathCaseFollowsThePlatform)
{
  RecentProjects recent(m_file);
  recent.recordOpened(project("Courtyard"), at(baseTime));
  recent.recordOpened(project("COURTYARD"), at(baseTime + 1));

#if defined(_WIN32) || defined(__APPLE__)
  ASSERT_EQ(recent.entries().size(), 1u);
  EXPECT_EQ(recent.entries()[0].path, generic(project("COURTYARD")));
#else
  EXPECT_EQ(recent.entries().size(), 2u);
#endif

  recent.recordOpened(project("Different"), at(baseTime + 2));
  EXPECT_GE(recent.entries().size(), 2u);
}

TEST_F(RecentProjectsTest, CapEvictsTheOldestUnpinnedEntry)
{
  RecentProjects recent(m_file);
  for (int i = 0; i < 51; ++i)
  {
    recent.recordOpened(project("P" + std::to_string(i)), at(baseTime + i));
  }

  const auto entries = recent.entries();
  ASSERT_EQ(entries.size(), 50u);
  EXPECT_EQ(entries.front().path, generic(project("P50")));
  EXPECT_EQ(entries.back().path, generic(project("P1")));
}

TEST_F(RecentProjectsTest, CapKeepsPinnedEntriesEvenWhenOldest)
{
  std::string projects;
  for (int i = 0; i < 50; ++i)
  {
    const std::string seconds = (i < 10 ? "0" : "") + std::to_string(i);
    projects += std::string(i == 0 ? "" : ",") + R"({"path":")" + generic(project("Old" + std::to_string(i)))
                + R"(","lastOpenedUtc":"2026-01-01T00:00:)" + seconds + R"(Z","pinned":)"
                + (i == 0 ? "true" : "false") + "}";
  }
  writeFile(R"({"version":1,"projects":[)" + projects + "]}");

  RecentProjects recent(m_file);
  recent.load();
  ASSERT_EQ(recent.entries().size(), 50u);

  recent.recordOpened(project("New"), at(baseTime));

  bool hasPinnedOldest = false;
  bool hasUnpinnedOldest = false;
  for (const auto& entry : recent.entries())
  {
    hasPinnedOldest = hasPinnedOldest || entry.path == generic(project("Old0"));
    hasUnpinnedOldest = hasUnpinnedOldest || entry.path == generic(project("Old1"));
  }

  EXPECT_EQ(recent.entries().size(), 50u);
  EXPECT_TRUE(hasPinnedOldest);
  EXPECT_FALSE(hasUnpinnedOldest);
}

TEST_F(RecentProjectsTest, UnparseableTimeSortsAsOldest)
{
  std::string projects;
  for (int i = 0; i < 50; ++i)
  {
    projects += std::string(i == 0 ? "" : ",") + R"({"path":")" + generic(project("Old" + std::to_string(i)))
                + R"(","lastOpenedUtc":)" + (i == 7 ? R"("garbage")" : R"("2026-01-01T00:00:00Z")") + "}";
  }
  writeFile(R"({"projects":[)" + projects + "]}");

  RecentProjects recent(m_file);
  recent.load();
  recent.recordOpened(project("New"), at(baseTime));

  bool hasGarbage = false;
  bool hasNeighbor = false;
  for (const auto& entry : recent.entries())
  {
    hasGarbage = hasGarbage || entry.path == generic(project("Old7"));
    hasNeighbor = hasNeighbor || entry.path == generic(project("Old8"));
  }

  EXPECT_FALSE(hasGarbage);
  EXPECT_TRUE(hasNeighbor);
}

TEST_F(RecentProjectsTest, UnknownTopLevelFieldSurvivesAndTemplateIsNotInvented)
{
  writeFile(R"({"version":1,"futureFeature":{"x":[1,2]},"projects":[]})");

  RecentProjects recent(m_file);
  recent.load();
  recent.recordOpened(project("Fresh"), at(baseTime));
  ASSERT_TRUE(recent.save());

  const auto saved = readFile();
  EXPECT_EQ(saved.at("futureFeature"), nlohmann::json::parse(R"({"x":[1,2]})"));
  ASSERT_EQ(saved.at("projects").size(), 1u);
  EXPECT_FALSE(saved.at("projects").at(0).contains("template"));
  EXPECT_FALSE(saved.at("projects").at(0).contains("pinned"));
  EXPECT_TRUE(saved.at("projects").at(0).contains("path"));
}

TEST_F(RecentProjectsTest, CorruptFileIsEmptyAndReplacedOnSave)
{
  for (const std::string corrupt : { std::string("this is { not json"), std::string(R"({"projects": 5})") })
  {
    writeFile(corrupt);

    RecentProjects recent(m_file);
    EXPECT_NO_THROW(recent.load());
    EXPECT_TRUE(recent.entries().empty());

    recent.recordOpened(project("Recovered"), at(baseTime));
    ASSERT_TRUE(recent.save());

    const auto saved = readFile();
    EXPECT_EQ(saved.at("version"), 1);
    ASSERT_TRUE(saved.at("projects").is_array());
    EXPECT_EQ(saved.at("projects").size(), 1u);
  }
}

TEST_F(RecentProjectsTest, MalformedEntriesAreDroppedAndValidNeighborsKept)
{
  writeFile(R"({"version":1,"projects":[5,{"name":"no path"},{"path":7},)"
            R"({"path":")" + generic(project("Good")) + R"(","name":"Good"}]})");

  RecentProjects recent(m_file);
  recent.load();

  const auto entries = recent.entries();
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].path, generic(project("Good")));
}

TEST_F(RecentProjectsTest, NameIsTheFolderOrTheFileStem)
{
  RecentProjects recent(m_file);
  recent.recordOpened(project("Folder Project"), at(baseTime));
  recent.recordOpened(project("Legacy") / "Game.json", at(baseTime + 1));
  recent.recordOpened(project("Saved") / "Level.ecs3d", at(baseTime + 2));

  const auto entries = recent.entries();
  ASSERT_EQ(entries.size(), 3u);
  EXPECT_EQ(entries[0].name, "Level");
  EXPECT_EQ(entries[1].name, "Game");
  EXPECT_EQ(entries[2].name, "Folder Project");
}

TEST_F(RecentProjectsTest, RelativePathBecomesAbsolute)
{
  RecentProjects recent(m_file);
  recent.recordOpened(std::filesystem::path("relative-project"), at(baseTime));

  const auto path = std::filesystem::path(recent.entries()[0].path);
  EXPECT_TRUE(path.is_absolute());
  EXPECT_EQ(path.filename().string(), "relative-project");
}

TEST_F(RecentProjectsTest, TimestampFormatIsUtcAndExact)
{
  const std::regex shape(R"(^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$)");

  EXPECT_TRUE(std::regex_match(RecentProjects::formatUtc(Clock::now()), shape));
  EXPECT_EQ(RecentProjects::formatUtc(at(baseTime)), "2026-10-18T19:02:11Z");
  EXPECT_EQ(RecentProjects::formatUtc(at(0)), "1970-01-01T00:00:00Z");
  EXPECT_EQ(RecentProjects::formatUtc(at(1709251199)), "2024-02-29T23:59:59Z");

  RecentProjects recent(m_file);
  recent.recordOpened(project("Timed"), at(baseTime));
  EXPECT_EQ(recent.entries()[0].lastOpenedUtc, "2026-10-18T19:02:11Z");
}
