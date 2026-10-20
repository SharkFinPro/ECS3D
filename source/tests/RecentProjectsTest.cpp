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
#include <vector>

namespace {
  using Clock = std::chrono::system_clock;
  using Json = nlohmann::json;

  Clock::time_point at(const std::int64_t epochSeconds)
  {
    return Clock::time_point(std::chrono::duration_cast<Clock::duration>(std::chrono::seconds(epochSeconds)));
  }

  constexpr std::int64_t baseTime = 1792350131;  // 2026-10-18T19:02:11Z

  std::string generic(const std::filesystem::path& path)
  {
    const auto text = path.lexically_normal().generic_u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
  }

  std::filesystem::path fromUtf8(const std::string& text)
  {
    const auto* first = reinterpret_cast<const char8_t*>(text.data());
    return std::filesystem::path(first, first + text.size());
  }

  Json makeEntry(const std::string& path, const std::string& time, const bool pinned = false)
  {
    Json entry = { { "path", path }, { "lastOpenedUtc", time } };
    if (pinned)
    {
      entry["pinned"] = true;
    }

    return entry;
  }

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

    void writeEntries(const std::vector<Json>& entries) const
    {
      writeFile(Json{ { "version", 1 }, { "projects", entries } }.dump());
    }

    [[nodiscard]] Json readFile() const
    {
      std::ifstream in(m_file);
      return Json::parse(in);
    }

    [[nodiscard]] std::filesystem::path project(const std::string& name) const
    {
      return m_directory / name;
    }

    [[nodiscard]] std::vector<Json> fiftyEntries(const std::string& time) const
    {
      std::vector<Json> entries;
      for (int i = 0; i < 50; ++i)
      {
        entries.push_back(makeEntry(generic(project("Old" + std::to_string(i))), time));
      }

      return entries;
    }

    [[nodiscard]] static bool contains(const RecentProjects& recent, const std::string& path)
    {
      for (const auto& entry : recent.entries())
      {
        if (entry.path == path)
        {
          return true;
        }
      }

      return false;
    }

    // Whether Old7, holding this time among 50 entries of a valid older time, survives one more open.
    [[nodiscard]] bool old7SurvivesWithTime(const std::string& time)
    {
      auto entries = fiftyEntries("2026-01-01T00:00:10Z");
      entries[7] = makeEntry(generic(project("Old7")), time);
      writeEntries(entries);

      RecentProjects recent(m_file);
      recent.load();
      recent.recordOpened(project("New"), at(baseTime));

      return contains(recent, generic(project("Old7")));
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
  writeEntries({ Json{ { "path", generic(project("Courtyard")) }, { "name", "Courtyard" },
                       { "lastOpenedUtc", "2026-10-18T19:02:11Z" }, { "pinned", true },
                       { "template", "first-person" } } });

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
}

TEST_F(RecentProjectsTest, SaveLeavesNoTemporaryFileBehind)
{
  RecentProjects recent(m_file);
  recent.recordOpened(project("A"), at(baseTime));
  ASSERT_TRUE(recent.save());
  ASSERT_TRUE(recent.save());

  int files = 0;
  for (const auto& item : std::filesystem::directory_iterator(m_directory))
  {
    EXPECT_NE(item.path().extension().string(), ".tmp");
    ++files;
  }

  EXPECT_EQ(files, 1);
  EXPECT_TRUE(std::filesystem::exists(m_file));
}

TEST_F(RecentProjectsTest, ReopeningMovesToFrontKeepingPinnedTemplateAndUnknownFields)
{
  Json second = makeEntry(generic(project("Second")), "2026-10-17T19:02:11Z", true);
  second["name"] = "stale";
  second["template"] = "empty-3d";
  second["custom"] = { { "a", 1 } };
  writeEntries({ makeEntry(generic(project("First")), "2026-10-18T19:02:11Z"), second });

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

  EXPECT_EQ(readFile().at("projects").at(0).at("custom"), Json({ { "a", 1 } }));
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

TEST_F(RecentProjectsTest, RootKeepsItsSeparator)
{
  const auto root = m_directory.root_path();

  RecentProjects recent(m_file);
  recent.recordOpened(root, at(baseTime));

  ASSERT_EQ(recent.entries().size(), 1u);
  EXPECT_EQ(recent.entries()[0].path, generic(root));
  EXPECT_EQ(recent.entries()[0].path.back(), '/');
}

TEST_F(RecentProjectsTest, LoadedPathsAreNormalizedBeforeComparing)
{
  writeEntries({ makeEntry(generic(project("Game")) + "/", "2026-01-01T00:00:00Z") });

  RecentProjects recent(m_file);
  recent.load();
  recent.recordOpened(project("Game"), at(baseTime));
  EXPECT_EQ(recent.entries().size(), 1u);

  recent.recordOpened(project("Another"), at(baseTime + 1));
  EXPECT_EQ(recent.entries().size(), 2u);

#if defined(_WIN32)
  auto native = project("Win").make_preferred().string();
  writeEntries({ makeEntry(native + "\\", "2026-01-01T00:00:00Z") });

  RecentProjects windows(m_file);
  windows.load();
  windows.recordOpened(project("Win"), at(baseTime));
  EXPECT_EQ(windows.entries().size(), 1u);
#endif
}

#if defined(_WIN32)
TEST_F(RecentProjectsTest, BackslashesAreTheSamePathOnWindows)
{
  RecentProjects recent(m_file);
  recent.recordOpened(std::filesystem::path(generic(project("Game"))), at(baseTime));
  recent.recordOpened(std::filesystem::path(project("Game").make_preferred().native()), at(baseTime + 1));

  ASSERT_EQ(recent.entries().size(), 1u);
  EXPECT_EQ(recent.entries()[0].path.find('\\'), std::string::npos);

  recent.recordOpened(project("Other"), at(baseTime + 2));
  EXPECT_EQ(recent.entries().size(), 2u);
}
#endif

TEST_F(RecentProjectsTest, PathCaseFollowsThePlatform)
{
  RecentProjects recent(m_file);
  recent.recordOpened(project("Courtyard"), at(baseTime));
  recent.recordOpened(project("COURTYARD"), at(baseTime + 1));
  recent.recordOpened(project("Different"), at(baseTime + 2));

#if defined(_WIN32) || defined(__APPLE__)
  ASSERT_EQ(recent.entries().size(), 2u);
  EXPECT_EQ(recent.entries()[1].path, generic(project("COURTYARD")));
#else
  EXPECT_EQ(recent.entries().size(), 3u);
#endif
}

TEST_F(RecentProjectsTest, DuplicatesInALoadedFileCollapseIntoTheFirst)
{
  Json first = makeEntry(generic(project("Dup")), "2026-01-01T00:00:00Z");
  first["template"] = "t1";
  Json second = makeEntry(generic(project("Dup")) + "/", "2026-01-02T00:00:00Z", true);
  second["template"] = "t2";

  std::vector<Json> entries = { first, makeEntry(generic(project("Other")), "2026-01-03T00:00:00Z"), second };
#if defined(_WIN32)
  entries.push_back(makeEntry(project("Dup").make_preferred().string(), "2026-01-04T00:00:00Z"));
#endif
  writeEntries(entries);

  RecentProjects recent(m_file);
  recent.load();
  ASSERT_GE(recent.entries().size(), 3u);

  recent.recordOpened(project("Dup"), at(baseTime));

  const auto result = recent.entries();
  ASSERT_EQ(result.size(), 2u);
  EXPECT_EQ(result[0].path, generic(project("Dup")));
  EXPECT_EQ(result[0].templateId, "t1");
  EXPECT_FALSE(result[0].pinned);
  EXPECT_EQ(result[1].path, generic(project("Other")));
}

TEST_F(RecentProjectsTest, ProjectFilesAreRecordedAsTheirFolder)
{
  RecentProjects recent(m_file);
  recent.recordOpened(project("Courtyard") / "project.ecs3d", at(baseTime));
  recent.recordOpened(project("Courtyard") / "PROJECT.ECS3D", at(baseTime + 1));
  recent.recordOpened(project("Courtyard"), at(baseTime + 2));
  recent.recordOpened(project("Saved") / "Level.ecs3d", at(baseTime + 3));
  recent.recordOpened(project("Legacy") / "Game.json", at(baseTime + 4));

  const auto entries = recent.entries();
  ASSERT_EQ(entries.size(), 3u);
  EXPECT_EQ(entries[0].path, generic(project("Legacy") / "Game.json"));
  EXPECT_EQ(entries[0].name, "Game");
  EXPECT_EQ(entries[1].path, generic(project("Saved")));
  EXPECT_EQ(entries[1].name, "Saved");
  EXPECT_EQ(entries[2].path, generic(project("Courtyard")));
  EXPECT_EQ(entries[2].name, "Courtyard");
}

TEST_F(RecentProjectsTest, CapEvictsTheOldestUnpinnedEntryWhereverItSits)
{
  std::vector<Json> entries;
  for (int i = 0; i < 50; ++i)
  {
    const int seconds = i * 7 % 50;
    entries.push_back(makeEntry(generic(project("Old" + std::to_string(i))),
                                std::string("2026-01-01T00:00:") + (seconds < 10 ? "0" : "")
                                  + std::to_string(seconds) + "Z"));
  }
  writeEntries(entries);

  RecentProjects recent(m_file);
  recent.load();
  recent.recordOpened(project("New"), at(baseTime));

  ASSERT_EQ(recent.entries().size(), 50u);
  EXPECT_EQ(recent.entries().front().path, generic(project("New")));
  EXPECT_FALSE(contains(recent, generic(project("Old0"))));
  EXPECT_TRUE(contains(recent, generic(project("Old1"))));
  EXPECT_TRUE(contains(recent, generic(project("Old49"))));
}

TEST_F(RecentProjectsTest, CapKeepsPinnedEntriesEvenWhenOldest)
{
  auto entries = fiftyEntries("2026-01-01T00:00:10Z");
  entries[0] = makeEntry(generic(project("Old0")), "2026-01-01T00:00:00Z", true);
  entries[1] = makeEntry(generic(project("Old1")), "2026-01-01T00:00:01Z");
  writeEntries(entries);

  RecentProjects recent(m_file);
  recent.load();
  recent.recordOpened(project("New"), at(baseTime));

  EXPECT_EQ(recent.entries().size(), 50u);
  EXPECT_TRUE(contains(recent, generic(project("Old0"))));
  EXPECT_FALSE(contains(recent, generic(project("Old1"))));
}

TEST_F(RecentProjectsTest, JustRecordedEntryIsKeptWhenEveryOtherIsPinned)
{
  std::vector<Json> entries;
  for (int i = 0; i < 50; ++i)
  {
    entries.push_back(makeEntry(generic(project("Pinned" + std::to_string(i))), "2026-01-01T00:00:10Z", true));
  }
  writeEntries(entries);

  RecentProjects recent(m_file);
  recent.load();
  recent.recordOpened(project("New"), at(baseTime));

  ASSERT_EQ(recent.entries().size(), 51u);
  EXPECT_EQ(recent.entries().front().path, generic(project("New")));
  EXPECT_TRUE(contains(recent, generic(project("Pinned0"))));
}

TEST_F(RecentProjectsTest, JustRecordedEntryOlderThanTheRestIsKept)
{
  auto entries = fiftyEntries("2026-01-01T00:00:10Z");
  entries[3] = makeEntry(generic(project("Old3")), "2026-01-01T00:00:02Z");
  writeEntries(entries);

  RecentProjects recent(m_file);
  recent.load();
  recent.recordOpened(project("New"), at(0));

  EXPECT_EQ(recent.entries().size(), 50u);
  EXPECT_EQ(recent.entries().front().path, generic(project("New")));
  EXPECT_FALSE(contains(recent, generic(project("Old3"))));
  EXPECT_TRUE(contains(recent, generic(project("Old4"))));
}

TEST_F(RecentProjectsTest, MalformedTimesSortAsOldest)
{
  // With equal valid times the later list entry goes, so Old7 survives; that is the control.
  EXPECT_TRUE(old7SurvivesWithTime("2026-01-01T00:00:10Z"));

  EXPECT_FALSE(old7SurvivesWithTime("garbage"));
  EXPECT_FALSE(old7SurvivesWithTime("2026-10-18T19:02: 1Z"));
  EXPECT_FALSE(old7SurvivesWithTime(" 026-10-18T19:02:11Z"));
  EXPECT_FALSE(old7SurvivesWithTime("2026-02-30T00:00:00Z"));
  EXPECT_FALSE(old7SurvivesWithTime("2026-13-01T00:00:00Z"));
  EXPECT_FALSE(old7SurvivesWithTime("2026-10-18T24:00:00Z"));
  EXPECT_FALSE(old7SurvivesWithTime("2026-10-18T00:00:60Z"));

  EXPECT_FALSE(old7SurvivesWithTime("2027-02-29T00:00:00Z"));
  EXPECT_FALSE(old7SurvivesWithTime("2100-02-29T00:00:00Z"));

  // Real leap days parse, so they are newer than the rest and survive.
  EXPECT_TRUE(old7SurvivesWithTime("2028-02-29T00:00:00Z"));
  EXPECT_TRUE(old7SurvivesWithTime("2400-02-29T00:00:00Z"));
}

TEST_F(RecentProjectsTest, TieOnEqualTimesEvictsTheLaterEntry)
{
  writeEntries(fiftyEntries("2026-01-01T00:00:10Z"));

  RecentProjects recent(m_file);
  recent.load();
  recent.recordOpened(project("New"), at(baseTime));

  EXPECT_FALSE(contains(recent, generic(project("Old49"))));
  EXPECT_TRUE(contains(recent, generic(project("Old0"))));
}

TEST_F(RecentProjectsTest, UnknownTopLevelFieldSurvivesAndTemplateIsNotInvented)
{
  writeFile(R"({"version":1,"futureFeature":{"x":[1,2]},"projects":[]})");

  RecentProjects recent(m_file);
  recent.load();
  recent.recordOpened(project("Fresh"), at(baseTime));
  ASSERT_TRUE(recent.save());

  const auto saved = readFile();
  EXPECT_EQ(saved.at("futureFeature"), Json::parse(R"({"x":[1,2]})"));
  ASSERT_EQ(saved.at("projects").size(), 1u);
  EXPECT_FALSE(saved.at("projects").at(0).contains("template"));
  EXPECT_FALSE(saved.at("projects").at(0).contains("pinned"));
  EXPECT_TRUE(saved.at("projects").at(0).contains("path"));
}

TEST_F(RecentProjectsTest, FileWithoutProjectsKeyIsEmptyButKeepsItsFields)
{
  writeFile(R"({"version":1,"futureFeature":true})");

  RecentProjects recent(m_file);
  recent.load();
  EXPECT_TRUE(recent.entries().empty());

  recent.recordOpened(project("Fresh"), at(baseTime));
  ASSERT_TRUE(recent.save());

  const auto saved = readFile();
  EXPECT_EQ(saved.at("futureFeature"), true);
  EXPECT_EQ(saved.at("projects").size(), 1u);
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

  const auto entries = recent.entries();
  ASSERT_EQ(entries.size(), 2u);
  EXPECT_EQ(entries[0].name, "Game");
  EXPECT_EQ(entries[1].name, "Folder Project");
}

TEST_F(RecentProjectsTest, NonAsciiPathsRoundTripAsUtf8)
{
  const std::string accent = "caf\xC3\xA9";
  const std::string cjk = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E";
  const auto base = generic(m_directory);

  RecentProjects recent(m_file);
  recent.recordOpened(fromUtf8(base + "/" + accent), at(baseTime));
  recent.recordOpened(fromUtf8(base + "/" + cjk), at(baseTime + 1));
  ASSERT_TRUE(recent.save());

  RecentProjects loaded(m_file);
  loaded.load();
  const auto entries = loaded.entries();

  ASSERT_EQ(entries.size(), 2u);
  EXPECT_EQ(entries[0].path, base + "/" + cjk);
  EXPECT_EQ(entries[0].name, cjk);
  EXPECT_EQ(entries[1].path, base + "/" + accent);
  EXPECT_EQ(entries[1].name, accent);
  EXPECT_EQ(readFile().at("projects").at(1).at("path").get<std::string>(), base + "/" + accent);

  loaded.recordOpened(fromUtf8(base + "/" + accent), at(baseTime + 2));
  EXPECT_EQ(loaded.entries().size(), 2u);
}

TEST_F(RecentProjectsTest, RelativePathBecomesAbsolute)
{
  RecentProjects recent(m_file);
  recent.recordOpened(std::filesystem::path("relative-project"), at(baseTime));

  const auto path = fromUtf8(recent.entries()[0].path);
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
  EXPECT_EQ(RecentProjects::formatUtc(at(4107542400)), "2100-03-01T00:00:00Z");

  RecentProjects recent(m_file);
  recent.recordOpened(project("Timed"), at(baseTime));
  EXPECT_EQ(recent.entries()[0].lastOpenedUtc, "2026-10-18T19:02:11Z");
}
