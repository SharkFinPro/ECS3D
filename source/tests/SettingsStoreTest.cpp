#include <gtest/gtest.h>

#include "SettingsStore.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
  class SettingsStoreTest : public testing::Test {
  protected:
    void SetUp() override
    {
      m_directory = std::filesystem::temp_directory_path() /
                    ("ecs3d-settings-" + std::string(testing::UnitTest::GetInstance()->current_test_info()->name()));

      remove_all(m_directory);
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
  SettingsStore store(m_file);
  store.load();

  EXPECT_EQ(store.get<bool>("missing", true), true);
  EXPECT_EQ(store.get<int>("missing", 7), 7);
  EXPECT_EQ(store.get<std::string>("missing", "fallback"), "fallback");
}

TEST_F(SettingsStoreTest, RoundTripsValuesThroughTheFile)
{
  {
    SettingsStore store(m_file);
    store.load();

    store.set("enabled", true);
    store.set("count", 7);
    store.set<std::string>("name", "editor");
    store.flush();
  }

  SettingsStore reloaded(m_file);
  reloaded.load();

  EXPECT_EQ(reloaded.get<bool>("enabled", false), true);
  EXPECT_EQ(reloaded.get<int>("count", 0), 7);
  EXPECT_EQ(reloaded.get<std::string>("name", ""), "editor");
}

TEST_F(SettingsStoreTest, CarriesThroughKeysItDoesNotKnow)
{
  writeFile(R"({"version": 99, "fromANewerBuild": {"nested": [1, 2, 3]}})");

  SettingsStore store(m_file);
  store.load();
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
  store.load();
  store.set("anything", 1);
  store.flush();

  EXPECT_TRUE(readFile().contains("version"));
}

TEST_F(SettingsStoreTest, MovesAnUnparseableFileAsideRatherThanOverwritingIt)
{
  writeFile("{ this is not json");

  SettingsStore store(m_file);
  store.load();

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

  SettingsStore store(m_file);
  store.load();

  auto spoiled = m_file;
  spoiled += ".bad";

  EXPECT_TRUE(exists(spoiled));
  EXPECT_EQ(store.get<int>("anything", 5), 5);
}

TEST_F(SettingsStoreTest, TreatsAKeyOfTheWrongTypeAsAbsent)
{
  writeFile(R"({"count": "not a number"})");

  SettingsStore store(m_file);
  store.load();

  EXPECT_EQ(store.get<int>("count", 42), 42);
}

TEST_F(SettingsStoreTest, DefersTheWriteUntilTheDebounceElapses)
{
  SettingsStore store(m_file);
  store.load();

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

TEST_F(SettingsStoreTest, DoesNotScheduleAWriteForAnUnchangedValue)
{
  SettingsStore store(m_file);
  store.load();

  store.set("count", 1);
  store.flush();

  store.set("count", 1);

  EXPECT_FALSE(store.hasPendingWrite());
}

TEST_F(SettingsStoreTest, FlushesOnDestruction)
{
  {
    SettingsStore store(m_file);
    store.load();
    store.set("count", 1);
  }

  ASSERT_TRUE(exists(m_file));
  EXPECT_EQ(readFile().at("count"), 1);
}

TEST(SettingsStore, DefaultFileSitsUnderAPerUserECS3DDirectory)
{
  const auto file = SettingsStore::defaultFile();

  EXPECT_EQ(file.filename(), "settings.json");
  EXPECT_EQ(file.parent_path().filename(), "ECS3D");
}
