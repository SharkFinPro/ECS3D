#include <gtest/gtest.h>

#include "SettingsStore.h"

#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
  class SettingsStoreTest : public testing::Test {
  protected:
    void SetUp() override
    {
      const auto* info = testing::UnitTest::GetInstance()->current_test_info();

      m_directory = std::filesystem::temp_directory_path() /
                    ("ecs3d-" + std::string(info->test_suite_name()) + "-" + std::string(info->name()));

      std::error_code error;
      remove_all(m_directory, error);

      m_file = m_directory / "settings.json";
    }

    void TearDown() override
    {
      std::error_code error;
      remove_all(m_directory, error);
    }

    void writeFile(const std::string& contents) const
    {
      create_directories(m_directory);
      std::ofstream out(m_file, std::ios::trunc);
      out << contents;
    }

    [[nodiscard]] nlohmann::json readFile() const
    {
      std::ifstream in(m_file);
      nlohmann::json parsed;
      in >> parsed;

      return parsed;
    }

    std::filesystem::path m_directory;
    std::filesystem::path m_file;
  };
}

TEST_F(SettingsStoreTest, UsesTheFallbackWhenThereIsNoFile)
{
  const SettingsStore store(m_file);

  EXPECT_EQ(store.get<bool>("missing", true), true);
  EXPECT_EQ(store.get<int>("missing", 7), 7);
  EXPECT_EQ(store.get<std::string>("missing", "fallback"), "fallback");
}

TEST_F(SettingsStoreTest, RoundTripsValuesThroughTheFile)
{
  {
    SettingsStore store(m_file);

    store.set("enabled", true);
    store.set("count", 7);
    store.set<std::string>("name", "editor");
    store.flush();
  }

  const SettingsStore reloaded(m_file);

  EXPECT_EQ(reloaded.get<bool>("enabled", false), true);
  EXPECT_EQ(reloaded.get<int>("count", 0), 7);
  EXPECT_EQ(reloaded.get<std::string>("name", ""), "editor");
}

TEST_F(SettingsStoreTest, CarriesThroughKeysItDoesNotKnow)
{
  writeFile(R"({"version": 1, "fromANewerBuild": {"nested": [1, 2, 3]}})");

  SettingsStore store(m_file);
  store.set("mine", 1);
  store.flush();

  const auto contents = readFile();

  ASSERT_TRUE(contents.contains("fromANewerBuild"));
  EXPECT_EQ(contents.at("fromANewerBuild").at("nested").size(), 3u);
  EXPECT_EQ(contents.at("mine"), 1);
}

TEST_F(SettingsStoreTest, WritesAVersion)
{
  SettingsStore store(m_file);
  store.set("anything", 1);
  store.flush();

  EXPECT_TRUE(readFile().contains("version"));
}

TEST_F(SettingsStoreTest, LeavesANewerVersionStampAlone)
{
  writeFile(R"({"version": 99})");

  SettingsStore store(m_file);
  store.set("anything", 1);
  store.flush();

  // Rewriting it to this build's version would destroy the anchor a future migration reads.
  EXPECT_EQ(readFile().at("version"), 99);
}

TEST_F(SettingsStoreTest, MovesAnUnparseableFileAsideRatherThanOverwritingIt)
{
  writeFile("{ this is not json");

  const SettingsStore store(m_file);

  auto spoiled = m_file;
  spoiled += ".bad";

  EXPECT_FALSE(exists(m_file));
  ASSERT_TRUE(exists(spoiled));

  std::ifstream in(spoiled);
  std::string contents;
  std::getline(in, contents);
  EXPECT_EQ(contents, "{ this is not json");

  EXPECT_EQ(store.get<int>("anything", 5), 5);
}

TEST_F(SettingsStoreTest, MovesAsideAFileThatParsesButIsNotAnObject)
{
  writeFile("[1, 2, 3]");

  const SettingsStore store(m_file);

  auto spoiled = m_file;
  spoiled += ".bad";

  EXPECT_FALSE(exists(m_file));
  EXPECT_TRUE(exists(spoiled));
  EXPECT_EQ(store.get<int>("anything", 5), 5);
}

TEST_F(SettingsStoreTest, TreatsAKeyOfTheWrongTypeAsAbsent)
{
  writeFile(R"({"count": "not a number"})");

  const SettingsStore store(m_file);

  EXPECT_EQ(store.get<int>("count", 42), 42);
}

TEST_F(SettingsStoreTest, DefersTheWriteUntilTheDebounceElapses)
{
  SettingsStore store(m_file);

  store.set("count", 1);
  store.set("count", 2);

  // Well inside the debounce window, so the edits are still collapsed into one pending write.
  store.update();

  EXPECT_TRUE(store.hasPendingWrite());
  EXPECT_FALSE(exists(m_file));

  store.flush();

  EXPECT_FALSE(store.hasPendingWrite());
  EXPECT_EQ(readFile().at("count"), 2);
}

TEST_F(SettingsStoreTest, UpdateWritesOnceTheDebounceHasElapsed)
{
  SettingsStore store(m_file, std::chrono::milliseconds(0));

  store.set("count", 1);
  ASSERT_TRUE(store.hasPendingWrite());

  store.update();

  EXPECT_FALSE(store.hasPendingWrite());
  ASSERT_TRUE(exists(m_file));
  EXPECT_EQ(readFile().at("count"), 1);
}

TEST_F(SettingsStoreTest, DoesNotScheduleAWriteForAnUnchangedValue)
{
  SettingsStore store(m_file);

  store.set("count", 1);
  store.flush();

  store.set("count", 1);

  EXPECT_FALSE(store.hasPendingWrite());
}

TEST_F(SettingsStoreTest, KeepsTheWritePendingWhenItFails)
{
  // A file where the settings directory needs to be, so creating the directory cannot succeed.
  create_directories(m_directory);
  std::ofstream blocker(m_directory / "blocked");
  blocker << "not a directory";
  blocker.close();

  SettingsStore store(m_directory / "blocked" / "settings.json");
  store.set("count", 1);
  store.flush();

  // Forgetting the write would lose the setting for good: set() short-circuits on the unchanged value,
  // so nothing would ever ask for it again.
  EXPECT_TRUE(store.hasPendingWrite());
}

TEST_F(SettingsStoreTest, LeavesNoTemporaryFileBehind)
{
  SettingsStore store(m_file);
  store.set("count", 1);
  store.flush();

  auto temporary = m_file;
  temporary += ".tmp";

  EXPECT_FALSE(exists(temporary));
}

TEST_F(SettingsStoreTest, FlushesOnDestruction)
{
  {
    SettingsStore store(m_file);
    store.set("count", 1);
  }

  ASSERT_TRUE(exists(m_file));
  EXPECT_EQ(readFile().at("count"), 1);
}

TEST_F(SettingsStoreTest, DefaultFileIsAnAbsolutePathUnderAnECS3DDirectory)
{
  const auto file = SettingsStore::defaultFile();

  // Absolute is the assertion with teeth: the no-home fallback is relative, and it is a degraded path.
  EXPECT_TRUE(file.is_absolute());
  EXPECT_EQ(file.filename(), "settings.json");
  EXPECT_EQ(file.parent_path().filename(), "ECS3D");
}
