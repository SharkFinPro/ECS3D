#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "Log.h"
#include "LogSink.h"
#include "ProjectSerializer.h"
#include "RingBufferSink.h"
#include "assets/AssetRegistry.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
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

namespace {
  const std::string branchSceneUUID = "77777777-7777-7777-7777-777777777777";

  nlohmann::json sceneBlob(const std::string& name = "Main")
  {
    return {
      { "uuid", branchSceneUUID },
      { "name", name },
      { "objects", nlohmann::json::array() }
    };
  }

  nlohmann::json projectBlob(const nlohmann::json& scenes, const nlohmann::json& currentSceneUUID)
  {
    nlohmann::json blob = { { "assets", { { "scenes", scenes } } } };

    if (!currentSceneUUID.is_null())
    {
      blob["currentSceneUUID"] = currentSceneUUID;
    }

    return blob;
  }
}

TEST_F(ProjectSerializerLogTest, SerializingWithNoCurrentSceneWritesAnEmptyUuid)
{
  EXPECT_EQ(m_serializer->serialize().at("currentSceneUUID"), "");

  m_serializer->deserialize(projectBlob(nlohmann::json::array({ sceneBlob() }), branchSceneUUID));

  EXPECT_EQ(m_serializer->serialize().at("currentSceneUUID"), branchSceneUUID);
}

TEST_F(ProjectSerializerLogTest, ABlobWithoutScenesLoadsNone)
{
  m_serializer->deserialize({ { "assets", nlohmann::json::object() } });

  EXPECT_TRUE(m_sceneManager.getScenes().empty());
  EXPECT_EQ(m_sceneManager.getCurrentScene(), nullptr);

  m_serializer->deserialize(projectBlob(nlohmann::json::array({ sceneBlob() }), nullptr));

  EXPECT_EQ(m_sceneManager.getScenes().size(), 1u);
}

TEST_F(ProjectSerializerLogTest, ACurrentSceneUuidThatDoesNotResolveSelectsNothing)
{
  m_serializer->deserialize(projectBlob(nlohmann::json::array({ sceneBlob() }), "not-a-uuid"));
  EXPECT_EQ(m_sceneManager.getScenes().size(), 1u);
  EXPECT_EQ(m_sceneManager.getCurrentScene(), nullptr);

  m_serializer->deserialize(projectBlob(nlohmann::json::array({ sceneBlob() }),
                                        "88888888-8888-8888-8888-888888888888"));
  EXPECT_EQ(m_sceneManager.getScenes().size(), 1u);
  EXPECT_EQ(m_sceneManager.getCurrentScene(), nullptr);

  m_serializer->deserialize(projectBlob(nlohmann::json::array({ sceneBlob() }), branchSceneUUID));
  ASSERT_NE(m_sceneManager.getCurrentScene(), nullptr);
  EXPECT_EQ(uuids::to_string(m_sceneManager.getCurrentScene()->getUUID()), branchSceneUUID);
}

TEST_F(ProjectSerializerLogTest, ASceneThatFailsToLoadIsNamedInTheError)
{
  m_serializer->deserialize(projectBlob(nlohmann::json::array({ sceneBlob() }), branchSceneUUID));
  ASSERT_NE(m_sceneManager.getCurrentScene(), nullptr);

  auto broken = sceneBlob("Broken");
  broken["uuid"] = "99999999-9999-9999-9999-999999999999";
  broken.erase("objects");

  try
  {
    m_serializer->deserialize(projectBlob(nlohmann::json::array({ broken }), nullptr));
    FAIL() << "expected the scene load to throw";
  }
  catch (const std::runtime_error& e)
  {
    EXPECT_NE(std::string(e.what()).find("scene 'Broken': "), std::string::npos) << e.what();
  }

  // The failed load must not have touched the project that was already there.
  ASSERT_EQ(m_sceneManager.getScenes().size(), 1u);
  ASSERT_NE(m_sceneManager.getCurrentScene(), nullptr);
  EXPECT_EQ(uuids::to_string(m_sceneManager.getCurrentScene()->getUUID()), branchSceneUUID);
}

TEST_F(ProjectSerializerLogTest, LoadingMalformedJsonFailsAndLogsAnError)
{
  {
    std::ofstream out(m_file);
    out << "{ not json";
  }

  const auto sink = addRingBuffer();

  EXPECT_FALSE(m_serializer->load(m_file));
  EXPECT_TRUE(hasEntry(sink, LogLevel::error, LogCategory::assets));
}

TEST_F(ProjectSerializerLogTest, LoadingAnEmptyOrNonObjectProjectFailsAndLogsAnError)
{
  ASSERT_TRUE(m_serializer->save(m_file));
  ASSERT_TRUE(m_serializer->load(m_file));
  m_sceneManager.addScene(std::make_shared<SceneAsset>(
    uuids::uuid::from_string(branchSceneUUID).value(), "Keep", m_componentRegistry));
  const auto keptAsset = uuids::uuid::from_string("88888888-8888-8888-8888-888888888888").value();
  m_assetRegistry.registerAsset({ .uuid = keptAsset, .type = AssetType::Model, .path = "kept.obj" });

  for (const std::string content : { "{}", "[]", "null", R"({"assets":null})", R"({"assets":5})" })
  {
    {
      std::ofstream out(m_file);
      out << content;
    }

    const auto sink = addRingBuffer();
    ASSERT_EQ(sink->sequence(), 0u) << content;

    EXPECT_FALSE(m_serializer->load(m_file)) << content;
    EXPECT_TRUE(hasEntry(sink, LogLevel::error, LogCategory::assets)) << content;
    EXPECT_EQ(m_sceneManager.getScenes().size(), 1u) << content;
    EXPECT_NE(m_assetRegistry.getByUUID(keptAsset), nullptr) << content;
  }
}

TEST_F(ProjectSerializerLogTest, LoadingAMinimalProjectObjectStillSucceeds)
{
  {
    std::ofstream out(m_file);
    out << R"({"assets":{}})";
  }

  const auto sink = addRingBuffer();

  EXPECT_TRUE(m_serializer->load(m_file));
  EXPECT_FALSE(hasEntry(sink, LogLevel::error, LogCategory::assets));
}

TEST_F(ProjectSerializerLogTest, SavingToAnUnopenablePathFailsAndLogsAnError)
{
  const auto sink = addRingBuffer();

  EXPECT_FALSE(m_serializer->save(m_directory / "missing" / "project.json"));
  EXPECT_TRUE(hasEntry(sink, LogLevel::error, LogCategory::assets));

  const auto controlSink = addRingBuffer();

  EXPECT_TRUE(m_serializer->save(m_file));
  EXPECT_FALSE(hasEntry(controlSink, LogLevel::error, LogCategory::assets));
}
