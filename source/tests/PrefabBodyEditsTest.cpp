#include <gtest/gtest.h>

#include "TestScene.h"
#include "Replication.h"
#include "objects/Object.h"
#include "objects/components/Transform.h"

#include <PrefabBodyEdits.h>

#include <glm/vec3.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <uuid.h>
#include <vector>

namespace {
  struct Sent {
    uuids::uuid uuid;
    std::string name;
    std::string body;
  };

  std::string makeBody(const std::string& name)
  {
    fixtures::Scene authoring;
    return fixtures::addObject(authoring, name, glm::vec3(1, 2, 3))->serialize().dump();
  }

  uuids::uuid uuidOf(const std::string& body)
  {
    return uuids::uuid::from_string(nlohmann::json::parse(body).at("uuid").get<std::string>()).value();
  }

  class PrefabBodyEditsTest : public testing::Test {
  protected:
    PrefabBodyEditsTest()
      : m_edits(m_scene.componentRegistry),
        m_bodyA(makeBody("A")),
        m_bodyB(makeBody("B"))
    {
      m_edits.setSendCallback([this](const uuids::uuid& uuid, const std::string& name, const std::string& body) {
        m_sent.push_back({ uuid, name, body });
      });
    }

    void select(const std::string& body, const std::string& name)
    {
      m_edits.select(uuidOf(body), name, body);
    }

    void moveTo(const glm::vec3& position)
    {
      fixtures::transformOf(m_edits.object())->setPosition(position);
      m_edits.markValueEdit();
    }

    fixtures::Scene m_scene;
    PrefabBodyEdits m_edits;
    std::string m_bodyA;
    std::string m_bodyB;
    std::vector<Sent> m_sent;
  };
}

TEST_F(PrefabBodyEditsTest, ValueEditIsHeldWhileAWidgetIsActiveAndSentOnceWhenIdle)
{
  select(m_bodyA, "pathA");
  moveTo(glm::vec3(9, 8, 7));

  m_edits.endFrame(true);
  EXPECT_TRUE(m_sent.empty());

  m_edits.endFrame(false);
  ASSERT_EQ(m_sent.size(), 1u);
  EXPECT_EQ(m_sent[0].uuid, uuidOf(m_bodyA));
  EXPECT_EQ(m_sent[0].name, "pathA");
  EXPECT_EQ(m_sent[0].body, m_edits.object()->serialize().dump());
  EXPECT_NE(m_sent[0].body, m_bodyA);

  m_edits.endFrame(false);
  EXPECT_EQ(m_sent.size(), 1u);
}

TEST_F(PrefabBodyEditsTest, OnlyAppliedStructuralEditsDirtyTheBody)
{
  select(m_bodyA, "pathA");
  const auto objectUUID = m_edits.object()->getUUID();

  m_edits.queueStructuralEdit({ { "op", "removeComponent" },
                                { "object", uuids::to_string(objectUUID) },
                                { "type", "Camera" } });
  m_edits.endFrame(false);
  EXPECT_TRUE(m_sent.empty());

  m_edits.queueStructuralEdit(replication::buildRenameObject(objectUUID, "Renamed"));
  m_edits.endFrame(false);
  ASSERT_EQ(m_sent.size(), 1u);
  EXPECT_EQ(m_edits.object()->getName(), "Renamed");
  EXPECT_NE(m_sent[0].body, m_bodyA);
}

TEST_F(PrefabBodyEditsTest, SelectingAnotherPrefabSendsThePendingEditUnderTheOldRecord)
{
  select(m_bodyA, "pathA");
  moveTo(glm::vec3(4, 5, 6));
  const auto editedBodyA = m_edits.object()->serialize().dump();
  m_edits.endFrame(true);
  ASSERT_TRUE(m_sent.empty());

  select(m_bodyB, "pathB");

  ASSERT_EQ(m_sent.size(), 1u);
  EXPECT_EQ(m_sent[0].uuid, uuidOf(m_bodyA));
  EXPECT_EQ(m_sent[0].name, "pathA");
  EXPECT_EQ(m_sent[0].body, editedBodyA);
  EXPECT_NE(m_sent[0].body, m_bodyA);
  EXPECT_NE(m_sent[0].body, m_bodyB);
  EXPECT_EQ(m_edits.object()->getUUID(), uuidOf(m_bodyB));
  EXPECT_EQ(m_edits.object()->getName(), "B");
}

TEST_F(PrefabBodyEditsTest, SettleRunsTheCommitCallbackBeforeSending)
{
  select(m_bodyA, "pathA");
  m_edits.setCommitCallback([this] {
    moveTo(glm::vec3(2, 2, 2));
  });

  m_edits.settle();

  ASSERT_EQ(m_sent.size(), 1u);
  EXPECT_EQ(m_sent[0].body, m_edits.object()->serialize().dump());
}

TEST_F(PrefabBodyEditsTest, TheEchoOfASentBodyDoesNotRebuildButADifferentBodyDoes)
{
  select(m_bodyA, "pathA");
  const auto before = m_edits.object();
  moveTo(glm::vec3(9, 9, 9));
  m_edits.endFrame(false);
  ASSERT_EQ(m_sent.size(), 1u);

  EXPECT_FALSE(m_edits.select(uuidOf(m_bodyA), "pathA", m_sent[0].body));
  EXPECT_EQ(m_edits.object(), before);

  EXPECT_TRUE(m_edits.select(uuidOf(m_bodyA), "pathA", m_bodyA));
  EXPECT_NE(m_edits.object(), before);
}

TEST_F(PrefabBodyEditsTest, SettleWithNothingDirtySendsNothing)
{
  select(m_bodyA, "pathA");
  m_edits.settle();
  EXPECT_TRUE(m_sent.empty());

  moveTo(glm::vec3(3, 3, 3));
  m_edits.settle();
  EXPECT_EQ(m_sent.size(), 1u);

  m_edits.settle();
  EXPECT_EQ(m_sent.size(), 1u);
}

TEST_F(PrefabBodyEditsTest, SettleDiscardsEditsQueuedBeforeItRunsTheCommitCallback)
{
  select(m_bodyA, "pathA");
  const auto objectUUID = m_edits.object()->getUUID();

  m_edits.queueStructuralEdit(replication::buildRenameObject(objectUUID, "Stale"));
  m_edits.settle();

  EXPECT_EQ(m_edits.object()->getName(), "A");
  EXPECT_TRUE(m_sent.empty());

  m_edits.setCommitCallback([&] {
    m_edits.queueStructuralEdit(replication::buildRenameObject(objectUUID, "Committed"));
  });
  m_edits.settle();

  EXPECT_EQ(m_edits.object()->getName(), "Committed");
  EXPECT_EQ(m_sent.size(), 1u);
}
