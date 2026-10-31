#include <gtest/gtest.h>

#include "Log.h"
#include "LogSetup.h"
#include "RingBufferSink.h"
#include "SettingsStore.h"
#include "UserDataDirectory.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <system_error>
#include <vector>

namespace {
  std::string readFile(const std::filesystem::path& file)
  {
    std::ifstream in(file);

    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }

  class LogFilePathTest : public testing::Test {
  protected:
    void SetUp() override
    {
      std::random_device randomDevice;
      m_directory = std::filesystem::temp_directory_path() /
                    ("ecs3d-logsetup-" + std::to_string(randomDevice()));

      std::error_code error;
      std::filesystem::remove_all(m_directory, error);
      std::filesystem::create_directories(m_directory);

      m_file = m_directory / "app.log";
    }

    // Cleanup lives here rather than at the end of each test so an early ASSERT_* cannot leave a sink
    // registered for every later suite to write through.
    void TearDown() override
    {
      removeSink();

      std::error_code error;
      std::filesystem::remove_all(m_directory, error);
    }

    // addFileSinkFromArguments takes a C-style argv, so the strings have to outlive the call.
    std::shared_ptr<FileSink> addSink(const std::vector<std::string>& arguments,
                                      const LogCategory category = LogCategory::engine)
    {
      removeSink();

      std::vector<std::string> storage{ "app" };
      storage.insert(storage.end(), arguments.begin(), arguments.end());

      std::vector<char*> argv;
      argv.reserve(storage.size());
      for (auto& argument : storage)
      {
        argv.push_back(argument.data());
      }

      m_addedSink = addFileSinkFromArguments(static_cast<int>(argv.size()), argv.data(), "testapp",
                                             category);

      return m_addedSink;
    }

    void removeSink()
    {
      if (m_addedSink)
      {
        Log::removeSink(m_addedSink);
        m_addedSink.reset();
      }
    }

    std::filesystem::path m_directory;
    std::filesystem::path m_file;
    std::shared_ptr<FileSink> m_addedSink;
  };
}

TEST_F(LogFilePathTest, DefaultLogFileSitsInALogsDirectoryUnderTheUserDataDirectory)
{
  const auto file = defaultLogFile("editor");

  EXPECT_EQ(file.filename(), "editor.log");
  EXPECT_EQ(file.parent_path().filename(), "logs");
  EXPECT_EQ(file.parent_path().parent_path(), userDataDirectory());
}

TEST_F(LogFilePathTest, TheUserDataDirectoryIsNamedECS3D)
{
  EXPECT_EQ(userDataDirectory().filename(), "ECS3D");
}

TEST_F(LogFilePathTest, SettingsAndLogsShareTheUserDataDirectory)
{
  EXPECT_EQ(SettingsStore::defaultFile().parent_path(), userDataDirectory());
}

TEST_F(LogFilePathTest, ALogFileArgumentQuotesAnAbsolutePathForAChildCommandLine)
{
  const auto file = std::filesystem::absolute(std::filesystem::path("data") / "my logs" / "editor.log");

  EXPECT_EQ(logFileArgumentForPath(file), "--log-file \"" + file.string() + "\"");
}

TEST_F(LogFilePathTest, ALogFileArgumentIsDroppedWhenThePathContainsAQuote)
{
  // No escape exists for a quote inside the path, so the flag is dropped rather than truncated.
  EXPECT_TRUE(logFileArgumentForPath(std::filesystem::path("data") / "my\"logs" / "editor.log").empty());
}

TEST_F(LogFilePathTest, LogFileArgumentOverridesTheDefaultAndReceivesEntries)
{
  const auto ring = std::make_shared<RingBufferSink>();
  Log::addSink(ring);

  const auto sink = addSink({ "--log-file", m_file.string() }, LogCategory::net);

  Log::removeSink(ring);

  ASSERT_NE(sink, nullptr);
  EXPECT_TRUE(sink->isOpen());
  ASSERT_TRUE(std::filesystem::exists(m_file));

  const auto announced = ring->snapshot();
  ASSERT_EQ(announced.size(), 1u);
  EXPECT_EQ(announced[0].level, LogLevel::info);
  EXPECT_EQ(announced[0].category, LogCategory::net);
  EXPECT_EQ(announced[0].message, "Logging to " + m_file.string());

  Log::info(LogCategory::engine, "through the registered sink");

  removeSink();

  const auto contents = readFile(m_file);
  EXPECT_NE(contents.find("through the registered sink"), std::string::npos);

  // Negative control: an entry written after the sink was removed must not land, so the line above
  // reaching the file is registration rather than the sink writing regardless.
  Log::info(LogCategory::engine, "after removal");
  EXPECT_EQ(readFile(m_file).find("after removal"), std::string::npos);
}

TEST_F(LogFilePathTest, ATrailingLogFileArgumentWithNoValueIsIgnored)
{
  const auto sink = addSink({ "--log-file", m_file.string(), "--log-file" });

  ASSERT_NE(sink, nullptr);
  EXPECT_TRUE(sink->isOpen());
  EXPECT_TRUE(std::filesystem::exists(m_file));
}

TEST_F(LogFilePathTest, NoLogFileArgumentRegistersNothing)
{
  EXPECT_EQ(addSink({ "--no-log-file" }), nullptr);

  EXPECT_EQ(addSink({ "--no-log-file", "--log-file", m_file.string() }), nullptr);
  EXPECT_FALSE(std::filesystem::exists(m_file));

  // Positive control: the same override without --no-log-file does create the file, so the absence
  // above is the flag winning rather than the override never working.
  const auto sink = addSink({ "--log-file", m_file.string() });
  ASSERT_NE(sink, nullptr);
  EXPECT_TRUE(std::filesystem::exists(m_file));
}

TEST_F(LogFilePathTest, ALogFileThatCannotBeOpenedWarnsAndRegistersNothing)
{
  // A directory where the file should go, so opening it for writing fails.
  const auto directoryPath = m_directory / "taken";
  std::filesystem::create_directories(directoryPath);

  const auto ring = std::make_shared<RingBufferSink>();
  Log::addSink(ring);

  const auto sink = addSink({ "--log-file", directoryPath.string() }, LogCategory::net);

  Log::removeSink(ring);

  EXPECT_EQ(sink, nullptr);

  const auto entries = ring->snapshot();
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].level, LogLevel::warn);
  EXPECT_EQ(entries[0].category, LogCategory::net);
  EXPECT_EQ(entries[0].message, "Could not open log file " + directoryPath.string());

  // Positive control: a usable path through the same call registers a sink.
  EXPECT_NE(addSink({ "--log-file", m_file.string() }), nullptr);
}

TEST_F(LogFilePathTest, ALogFileArgumentNamesEveryAppsDefaultFile)
{
  for (const char* appName : { "editor", "client", "server", "editor-server", "client-server" })
  {
    const auto argument = logFileArgument(appName);

    // A quote in the real user data path yields no flag; the quoting itself is pinned above.
    if (argument.empty())
    {
      continue;
    }

    EXPECT_EQ(argument.rfind("--log-file \"", 0), 0u) << appName;
    EXPECT_TRUE(argument.ends_with(std::string(appName) + ".log\"")) << appName;
    EXPECT_TRUE(std::filesystem::path(argument.substr(12, argument.size() - 13)).is_absolute()) << appName;
  }
}

namespace {
  std::filesystem::path resolveWith(const std::map<std::string, std::string>& environment)
  {
    return userDataDirectory([&environment](const char* name) -> const char*
    {
      const auto it = environment.find(name);

      return it == environment.end() ? nullptr : it->second.c_str();
    });
  }
}

TEST(UserDataDirectory, UsesThePlatformsPreferredVariable)
{
#ifdef _WIN32
  EXPECT_EQ(resolveWith({ { "APPDATA", "/data/roaming" } }), std::filesystem::path("/data/roaming") / "ECS3D");
#elif defined(__APPLE__)
  EXPECT_EQ(resolveWith({ { "HOME", "/Users/me" } }),
            std::filesystem::path("/Users/me") / "Library" / "Application Support" / "ECS3D");
#else
  EXPECT_EQ(resolveWith({ { "XDG_CONFIG_HOME", "/cfg" }, { "HOME", "/home/me" } }),
            std::filesystem::path("/cfg") / "ECS3D");
#endif
}

TEST(UserDataDirectory, FallsBackToHomeWhereThePlatformHasASecondChoice)
{
#if !defined(_WIN32) && !defined(__APPLE__)
  EXPECT_EQ(resolveWith({ { "HOME", "/home/me" } }), std::filesystem::path("/home/me") / ".config" / "ECS3D");
  EXPECT_EQ(resolveWith({ { "XDG_CONFIG_HOME", "" }, { "HOME", "/home/me" } }),
            std::filesystem::path("/home/me") / ".config" / "ECS3D");
#else
  GTEST_SKIP() << "This platform resolves from a single variable";
#endif
}

TEST(UserDataDirectory, TreatsAnEmptyValueAsUnset)
{
#ifdef _WIN32
  const char* variable = "APPDATA";
#else
  const char* variable = "HOME";
#endif

  EXPECT_EQ(resolveWith({ { variable, "" } }), std::filesystem::path("ECS3D"));
}

TEST(UserDataDirectory, IsTheRelativeDirectoryWhenNothingIsSet)
{
  const auto directory = resolveWith({});

  EXPECT_EQ(directory, std::filesystem::path("ECS3D"));
  EXPECT_TRUE(directory.is_relative());
}
