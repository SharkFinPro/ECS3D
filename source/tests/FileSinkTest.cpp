#include <gtest/gtest.h>

#include "FileSink.h"
#include "Log.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {
  std::vector<std::string> readLines(const std::filesystem::path& file)
  {
    std::vector<std::string> lines;

    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line))
    {
      lines.push_back(line);
    }

    return lines;
  }

  class FileSinkTest : public testing::Test {
  protected:
    void SetUp() override
    {
      std::random_device randomDevice;
      m_directory = std::filesystem::temp_directory_path() /
                    ("ecs3d-filesink-" + std::to_string(randomDevice()));

      std::error_code error;
      std::filesystem::remove_all(m_directory, error);
      std::filesystem::create_directories(m_directory);

      m_file = m_directory / "engine.log";
    }

    void TearDown() override
    {
      if (m_addedSink)
      {
        Log::removeSink(m_addedSink);
        Log::setMinimumLevel(LogLevel::info);
      }

      std::error_code error;
      std::filesystem::remove_all(m_directory, error);
    }

    std::filesystem::path m_directory;
    std::filesystem::path m_file;
    std::shared_ptr<LogSink> m_addedSink;
  };
}

TEST_F(FileSinkTest, WritesOneLinePerEntryInOrder)
{
  FileSink sink(m_file);

  sink.write(LogEntry{ std::chrono::system_clock::now(), LogLevel::info, LogCategory::physics, "first" });
  sink.write(LogEntry{ std::chrono::system_clock::now(), LogLevel::error, LogCategory::net, "second" });

  const auto lines = readLines(m_file);

  ASSERT_EQ(lines.size(), 2u);
  EXPECT_NE(lines[0].find("[info][physics] first"), std::string::npos);
  EXPECT_NE(lines[1].find("[error][net] second"), std::string::npos);
}

TEST_F(FileSinkTest, FormatsTheTimestampDeterministically)
{
  FileSink sink(m_file);

  const std::chrono::system_clock::time_point time{ std::chrono::seconds{ 1767323045 } +
                                                      std::chrono::milliseconds{ 6 } };

  sink.write(LogEntry{ time, LogLevel::info, LogCategory::engine, "message" });

  const auto lines = readLines(m_file);

  ASSERT_EQ(lines.size(), 1u);
  EXPECT_TRUE(lines[0].starts_with("2026-01-02T03:04:05.006Z ["));
}

TEST_F(FileSinkTest, CreatesMissingParentDirectories)
{
  const auto file = m_directory / "a" / "b" / "engine.log";

  FileSink sink(file);

  EXPECT_TRUE(sink.isOpen());

  sink.write(LogEntry{ std::chrono::system_clock::now(), LogLevel::info, LogCategory::engine, "nested" });

  const auto lines = readLines(file);
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_NE(lines[0].find("nested"), std::string::npos);
}

TEST_F(FileSinkTest, TruncatesAnExistingFile)
{
  {
    std::ofstream stale(m_file, std::ios::trunc);
    stale << "stale line\n";
  }

  FileSink sink(m_file);
  sink.write(LogEntry{ std::chrono::system_clock::now(), LogLevel::info, LogCategory::engine, "fresh" });

  const auto lines = readLines(m_file);

  for (const auto& line : lines)
  {
    EXPECT_EQ(line.find("stale line"), std::string::npos);
  }

  // Positive control: the new entry actually landed, so the absence above is truncation rather than
  // write() silently failing.
  bool foundFresh = false;
  for (const auto& line : lines)
  {
    if (line.find("fresh") != std::string::npos)
    {
      foundFresh = true;
    }
  }
  EXPECT_TRUE(foundFresh);
}

TEST_F(FileSinkTest, AnUnopenablePathLeavesTheSinkClosedWithoutThrowing)
{
  // The fixture directory itself exists as a directory, so opening it as a file must fail.
  FileSink sink(m_directory);

  EXPECT_FALSE(sink.isOpen());
  EXPECT_NO_THROW(
    sink.write(LogEntry{ std::chrono::system_clock::now(), LogLevel::info, LogCategory::engine, "nowhere" }));

  // Positive control: a valid path in the same directory opens fine, so the failure above is about the
  // path being a directory rather than something wrong with the fixture directory in general.
  FileSink validSink(m_file);
  EXPECT_TRUE(validSink.isOpen());
}

TEST_F(FileSinkTest, WorksThroughTheLogFacade)
{
  auto sink = std::make_shared<FileSink>(m_file);
  m_addedSink = sink;
  Log::addSink(sink);

  Log::warn(LogCategory::assets, "via log");

  const auto lines = readLines(m_file);

  ASSERT_EQ(lines.size(), 1u);
  EXPECT_NE(lines[0].find("[warn][assets] via log"), std::string::npos);
}
