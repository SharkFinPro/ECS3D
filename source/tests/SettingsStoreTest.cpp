#include <gtest/gtest.h>

#include "EditorCameraSettings.h"
#include "Log.h"
#include "LogSink.h"
#include "RingBufferSink.h"
#include "SettingsStore.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {
  class SettingsStoreTest : public testing::Test {
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

      for (const auto& sink : m_addedSinks)
      {
        Log::removeSink(sink);
      }

      Log::setMinimumLevel(LogLevel::info);
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
      nlohmann::json parsed;
      in >> parsed;

      return parsed;
    }

    std::shared_ptr<RingBufferSink> addRingBuffer()
    {
      auto sink = std::make_shared<RingBufferSink>();
      Log::addSink(sink);
      m_addedSinks.push_back(sink);

      return sink;
    }

    std::filesystem::path m_directory;
    std::filesystem::path m_file;
    std::vector<std::shared_ptr<LogSink>> m_addedSinks;
  };

  bool hasAHomeDirectory()
  {
    for (const char* name : { "APPDATA", "XDG_CONFIG_HOME", "HOME" })
    {
      if (const char* value = std::getenv(name); value && *value)
      {
        return true;
      }
    }

    return false;
  }
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

  EXPECT_FALSE(std::filesystem::exists(m_file));
  ASSERT_TRUE(std::filesystem::exists(spoiled));

  std::ifstream in(spoiled);
  std::string contents;
  std::getline(in, contents);
  EXPECT_EQ(contents, "{ this is not json");

  EXPECT_EQ(store.get<int>("anything", 5), 5);
}

TEST_F(SettingsStoreTest, LoadingInvalidJsonLogsAWarningAndFallsBackToDefaults)
{
  writeFile("{ this is not json");

  const auto sink = addRingBuffer();

  const SettingsStore store(m_file);

  bool foundWarning = false;
  for (const auto& entry : sink->snapshot())
  {
    if (entry.level == LogLevel::warn && entry.category == LogCategory::editor)
    {
      foundWarning = true;
      break;
    }
  }

  EXPECT_TRUE(foundWarning);

  // Positive control: the defaults were actually used, so the warning above reflects the fallback path
  // rather than some unrelated warning happening to be logged.
  EXPECT_EQ(store.get<int>("anything", 5), 5);
}

TEST_F(SettingsStoreTest, MovesAsideAFileThatParsesButIsNotAnObject)
{
  writeFile("[1, 2, 3]");

  const SettingsStore store(m_file);

  auto spoiled = m_file;
  spoiled += ".bad";

  EXPECT_FALSE(std::filesystem::exists(m_file));
  EXPECT_TRUE(std::filesystem::exists(spoiled));
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
  EXPECT_FALSE(std::filesystem::exists(m_file));

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
  ASSERT_TRUE(std::filesystem::exists(m_file));
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

TEST_F(SettingsStoreTest, ClearingAKeyReturnsItToTheFallback)
{
  {
    SettingsStore store(m_file);
    store.set("count", 41);
    store.clear("count");

    EXPECT_EQ(store.get<int>("count", 7), 7);
  }

  // The point of clearing rather than writing the default back: the key has to be gone from the file,
  // so a later build shipping a different default is the one that applies.
  EXPECT_FALSE(readFile().contains("count"));

  const SettingsStore reloaded(m_file);
  EXPECT_EQ(reloaded.get<int>("count", 7), 7);
}

TEST_F(SettingsStoreTest, DoesNotScheduleAWriteForClearingAnAbsentKey)
{
  SettingsStore store(m_file);

  store.set("count", 1);
  store.flush();

  store.clear("missing");

  EXPECT_FALSE(store.hasPendingWrite());

  // The control: clearing a key that is there does schedule one, so the assertion above is about the
  // key being absent rather than about clear() never scheduling anything.
  store.clear("count");

  EXPECT_TRUE(store.hasPendingWrite());
}

TEST_F(SettingsStoreTest, KeepsTheWritePendingWhenItFails)
{
  // A file where the settings directory needs to be, so creating the directory cannot succeed.
  std::filesystem::create_directories(m_directory);
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

TEST_F(SettingsStoreTest, AFailedWriteLogsAnError)
{
  // A file where the settings directory needs to be, so creating the directory cannot succeed.
  std::filesystem::create_directories(m_directory);
  std::ofstream blocker(m_directory / "blocked");
  blocker << "not a directory";
  blocker.close();

  const auto sink = addRingBuffer();

  SettingsStore store(m_directory / "blocked" / "settings.json");
  store.set("count", 1);
  store.flush();

  bool foundError = false;
  for (const auto& entry : sink->snapshot())
  {
    if (entry.level == LogLevel::error && entry.category == LogCategory::editor)
    {
      foundError = true;
      break;
    }
  }

  EXPECT_TRUE(foundError);

  // Positive control: the write really did fail (still pending), so the error above reflects that
  // failure rather than some unrelated error being logged.
  EXPECT_TRUE(store.hasPendingWrite());
}

TEST_F(SettingsStoreTest, LeavesNoTemporaryFileBehind)
{
  SettingsStore store(m_file);
  store.set("count", 1);
  store.flush();

  auto temporary = m_file;
  temporary += ".tmp";

  EXPECT_FALSE(std::filesystem::exists(temporary));
}

TEST_F(SettingsStoreTest, FlushesOnDestruction)
{
  {
    SettingsStore store(m_file);
    store.set("count", 1);
  }

  ASSERT_TRUE(std::filesystem::exists(m_file));
  EXPECT_EQ(readFile().at("count"), 1);
}

TEST_F(SettingsStoreTest, DefaultFileSitsUnderAnECS3DDirectory)
{
  const auto file = SettingsStore::defaultFile();

  EXPECT_EQ(file.filename(), "settings.json");
  EXPECT_EQ(file.parent_path().filename(), "ECS3D");
}

TEST_F(SettingsStoreTest, DefaultFileIsAbsoluteWhenTheresAHomeToResolveAgainst)
{
  if (!hasAHomeDirectory())
  {
    GTEST_SKIP() << "No per-user directory in the environment; the relative fallback is expected.";
  }

  // Only meaningful when the environment actually names one: the fallback is deliberately relative, so
  // asserting absoluteness unconditionally would fail a container that sets none of these.
  EXPECT_TRUE(SettingsStore::defaultFile().is_absolute());
}

TEST_F(SettingsStoreTest, EditorCameraSpeedFallsBackToTheEngineDefault)
{
  const SettingsStore store(m_file);

  EXPECT_FLOAT_EQ(editorCameraSettings::readSpeed(store), editorCameraSettings::defaultSpeed);
}

TEST_F(SettingsStoreTest, EditorCameraSpeedRoundTripsThroughTheFile)
{
  {
    SettingsStore store(m_file);
    editorCameraSettings::writeSpeed(store, 4.5f);
    store.flush();
  }

  const SettingsStore reloaded(m_file);
  EXPECT_FLOAT_EQ(editorCameraSettings::readSpeed(reloaded), 4.5f);
}

TEST_F(SettingsStoreTest, EditorCameraSpeedClampsAnOutOfRangeStoredValue)
{
  writeFile(R"({"editor.viewport.cameraSpeed": 999.0})");

  const SettingsStore store(m_file);

  EXPECT_FLOAT_EQ(editorCameraSettings::readSpeed(store), editorCameraSettings::maxSpeed);

  // Positive control: a value already inside the range comes back unchanged, so the clamp above is
  // actually clamping rather than always snapping to the max.
  writeFile(R"({"editor.viewport.cameraSpeed": 2.0})");
  const SettingsStore inRange(m_file);
  EXPECT_FLOAT_EQ(editorCameraSettings::readSpeed(inRange), 2.0f);
}

TEST_F(SettingsStoreTest, EditorCameraSpeedClampsBelowTheMinimumOnWrite)
{
  SettingsStore store(m_file);

  editorCameraSettings::writeSpeed(store, -3.0f);

  EXPECT_FLOAT_EQ(editorCameraSettings::readSpeed(store), editorCameraSettings::minSpeed);
}

TEST_F(SettingsStoreTest, EditorCameraSpeedRejectsANonFiniteValue)
{
  SettingsStore store(m_file);

  editorCameraSettings::writeSpeed(store, std::numeric_limits<float>::infinity());

  EXPECT_FLOAT_EQ(editorCameraSettings::readSpeed(store), editorCameraSettings::defaultSpeed);
}
