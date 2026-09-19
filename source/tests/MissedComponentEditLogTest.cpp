#include <gtest/gtest.h>

#include "Log.h"
#include "RingBufferSink.h"
#include "TestScene.h"
#include "Replication.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Transform.h"

#include <Protocol.h>
#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <uuid.h>
#include <vector>

namespace {
  using fixtures::transformOf;

  struct Scene : fixtures::Scene {
    std::shared_ptr<Object> object;
  };

  Scene makeScene()
  {
    Scene scene;
    scene.object = addObject(scene, "Object");

    return scene;
  }

  // Same trick as ComponentEditTest: drop the tail of an otherwise-valid edit so a component unpacks
  // part way through and reports partiallyApplied rather than malformedPayload.
  net::Message withoutTheLastBytes(const net::Message& source, const std::size_t dropped)
  {
    net::Message result(net::MessageType::editComponent);

    const auto bytes = source.bytes();
    for (std::size_t i = 0; i + dropped < bytes.size(); ++i)
    {
      result.write(bytes[i]);
    }

    return result;
  }

  class MissedComponentEditLogTest : public testing::Test {
  protected:
    void TearDown() override
    {
      for (const auto& sink : m_addedSinks)
      {
        Log::removeSink(sink);
      }

      Log::setMinimumLevel(LogLevel::info);
    }

    std::shared_ptr<RingBufferSink> addRingBuffer()
    {
      Log::setMinimumLevel(LogLevel::trace);

      auto sink = std::make_shared<RingBufferSink>();
      Log::addSink(sink);
      m_addedSinks.push_back(sink);

      return sink;
    }

    std::vector<std::shared_ptr<LogSink>> m_addedSinks;
  };
}

TEST_F(MissedComponentEditLogTest, LogsADebugEntryForAnUnknownObject)
{
  const auto sink = addRingBuffer();

  const auto scene = makeScene();

  // Built against another scene, so this scene has no object with that uuid - the routine case of a
  // rebroadcast for an object this view has not been sent yet or has already dropped.
  const auto elsewhere = makeScene();
  const auto edit = replication::buildComponentEdit(elsewhere.object->getUUID(), transformOf(elsewhere.object));

  const auto result = replication::applyComponentEdit(*scene.objectManager, edit);
  ASSERT_EQ(result, replication::ComponentEditResult::unknownObject);

  replication::logMissedComponentEdit(result, edit, LogCategory::client);

  const auto entries = sink->snapshot();
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].level, LogLevel::debug);
  EXPECT_EQ(entries[0].category, LogCategory::client);
  EXPECT_NE(entries[0].message.find(uuids::to_string(elsewhere.object->getUUID())), std::string::npos);
  EXPECT_NE(entries[0].message.find("Transform"), std::string::npos);
}

TEST_F(MissedComponentEditLogTest, LogsAnErrorEntryForAPartiallyAppliedEdit)
{
  const auto sink = addRingBuffer();

  const auto scene = makeScene();
  const auto transform = transformOf(scene.object);

  const auto fullEdit = replication::buildComponentEdit(scene.object->getUUID(), transform);
  const auto truncated = withoutTheLastBytes(fullEdit, 4);

  const auto result = replication::applyComponentEdit(*scene.objectManager, truncated);
  ASSERT_EQ(result, replication::ComponentEditResult::partiallyApplied);

  replication::logMissedComponentEdit(result, truncated, LogCategory::editor);

  const auto entries = sink->snapshot();
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].level, LogLevel::error);
  EXPECT_EQ(entries[0].category, LogCategory::editor);
  EXPECT_NE(entries[0].message.find("out of sync"), std::string::npos);
}

TEST_F(MissedComponentEditLogTest, LogsNothingForAnAppliedEdit)
{
  const auto sink = addRingBuffer();

  const auto scene = makeScene();
  const auto transform = transformOf(scene.object);

  const auto edit = replication::buildComponentEdit(scene.object->getUUID(), transform);

  const auto result = replication::applyComponentEdit(*scene.objectManager, edit);
  ASSERT_EQ(result, replication::ComponentEditResult::applied);

  // Positive control: a successful edit really did go through the helper and produced no entry, rather
  // than the sink simply never receiving anything.
  replication::logMissedComponentEdit(result, edit, LogCategory::client);

  EXPECT_TRUE(sink->snapshot().empty());
}

TEST(MissedComponentEditDescribe, DescribesEveryResult)
{
  EXPECT_EQ(replication::describe(replication::ComponentEditResult::applied), "it was applied");
  EXPECT_EQ(replication::describe(replication::ComponentEditResult::malformedPayload),
            "the payload does not parse");
  EXPECT_EQ(replication::describe(replication::ComponentEditResult::partiallyApplied),
            "the payload ran out mid-component");
  EXPECT_EQ(replication::describe(replication::ComponentEditResult::unknownObject), "no such object");
  EXPECT_EQ(replication::describe(replication::ComponentEditResult::unknownComponent),
            "the object has no such component");
}
