#include <gtest/gtest.h>

#include "Log.h"
#include "LogSetup.h"
#include "SettingsStore.h"
#include "UserDataDirectory.h"

#include <filesystem>
#include <fstream>
#include <iterator>
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
    std::shared_ptr<FileSink> addSink(const std::vector<std::string>& arguments)
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
                                             LogCategory::engine);

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

TEST_F(LogFilePathTest, ALogFileArgumentQuotesThePathForAChildCommandLine)
{
  const auto argument = logFileArgument("editor-server");

  EXPECT_TRUE(argument.starts_with("--log-file \""));
  EXPECT_TRUE(argument.ends_with("\""));
  EXPECT_NE(argument.find(defaultLogFile("editor-server").string()), std::string::npos);
}

TEST_F(LogFilePathTest, LogFileArgumentOverridesTheDefaultAndReceivesEntries)
{
  const auto sink = addSink({ "--log-file", m_file.string() });

  ASSERT_NE(sink, nullptr);
  EXPECT_TRUE(sink->isOpen());
  ASSERT_TRUE(std::filesystem::exists(m_file));

  Log::info(LogCategory::engine, "through the registered sink");

  removeSink();

  const auto contents = readFile(m_file);
  EXPECT_NE(contents.find("through the registered sink"), std::string::npos);

  // Negative control: an entry written after the sink was removed must not land, so the line above
  // reaching the file is registration rather than the sink writing regardless.
  Log::info(LogCategory::engine, "after removal");
  EXPECT_EQ(readFile(m_file).find("after removal"), std::string::npos);
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
