#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "Log.h"
#include "LogSink.h"
#include "ProjectSerializer.h"
#include "RingBufferSink.h"
#include "assets/AssetRegistry.h"
#include "scenes/SceneManager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {
  class ProjectSerializerLogTest : public testing::Test {
  protected:
    void SetUp() override
    {
      const auto* info = testing::UnitTest::GetInstance()->current_test_info();

      m_directory = std::filesystem::temp_directory_path() /
                    ("ecs3d-" + std::string(info->test_suite_name()) + "-" + std::string(info->name()));

      std::error_code error;
      std::filesystem::remove_all(m_directory, error);
      std::filesystem::create_directories(m_directory);

      m_file = (m_directory / "project.json").string();

      m_componentRegistry = std::make_shared<ComponentRegistry>();
      registerDataComponents(*m_componentRegistry);

      m_serializer = std::make_unique<ProjectSerializer>(&m_assetRegistry, &m_sceneManager, m_componentRegistry);
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

    std::shared_ptr<RingBufferSink> addRingBuffer()
    {
      auto sink = std::make_shared<RingBufferSink>();
      Log::addSink(sink);
      m_addedSinks.push_back(sink);

      return sink;
    }

    [[nodiscard]] static bool hasEntry(const std::shared_ptr<RingBufferSink>& sink, const LogLevel level,
                                       const LogCategory category)
    {
      const auto entries = sink->snapshot();
      return std::ranges::any_of(entries, [&](const auto& entry) {
        return entry.level == level && entry.category == category;
      });
    }

    std::filesystem::path m_directory;
    std::string m_file;
    AssetRegistry m_assetRegistry;
    SceneManager m_sceneManager;
    std::shared_ptr<ComponentRegistry> m_componentRegistry;
    std::unique_ptr<ProjectSerializer> m_serializer;
    std::vector<std::shared_ptr<LogSink>> m_addedSinks;
  };
}

TEST_F(ProjectSerializerLogTest, LoadingAMissingProjectLogsAnError)
{
  const auto sink = addRingBuffer();

  EXPECT_FALSE(m_serializer->load(m_file));

  EXPECT_TRUE(hasEntry(sink, LogLevel::error, LogCategory::assets));
}

TEST_F(ProjectSerializerLogTest, LoadingAValidProjectLogsNoError)
{
  ASSERT_TRUE(m_serializer->save(m_file));

  const auto sink = addRingBuffer();

  // Positive control: a successful load must not write an error entry, so the negative check below
  // actually reflects success rather than the sink simply never receiving anything.
  EXPECT_TRUE(m_serializer->load(m_file));

  EXPECT_FALSE(hasEntry(sink, LogLevel::error, LogCategory::assets));
}
