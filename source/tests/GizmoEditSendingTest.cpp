#include "TestScene.h"
#include <GizmoEditSending.h>
#include <gtest/gtest.h>
#include <objects/Object.h>
#include <objects/components/Transform.h>
#include <cstddef>
#include <glm/vec3.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <utility>
#include <uuid.h>
#include <vector>

namespace {
  struct Rig {
    fixtures::Scene scene;
    std::shared_ptr<Object> object;
    std::shared_ptr<Transform> transform;
    GizmoEditSending sending;

    std::vector<nlohmann::json> perFrame;
    std::vector<uuids::uuid> frameTargets;
    std::vector<const Component*> frameComponents;
    std::vector<std::pair<nlohmann::json, nlohmann::json>> commits;
    std::vector<uuids::uuid> committedTargets;

    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    Rig()
    {
      object = addObject(scene, "Object");
      transform = fixtures::transformOf(object);

      sending.setEditCallback([this](const uuids::uuid& id, const std::shared_ptr<Component>& component)
      {
        frameTargets.push_back(id);
        frameComponents.push_back(component.get());
        perFrame.push_back(transform->serialize());
      });
      sending.setEditCommittedCallback([this](const uuids::uuid& target, const nlohmann::json& before,
                                              const nlohmann::json& after)
      {
        committedTargets.push_back(target);
        commits.emplace_back(before, after);
      });
    }

    [[nodiscard]] uuids::uuid target() const
    {
      return object->getUUID();
    }

    void framePose(const gizmo::Pose& pose, const bool started = false, const bool finished = false)
    {
      gizmo::Frame f;
      f.dragStarted = started;
      f.dragFinished = finished;
      f.local = pose;

      sending.apply(f, target(), transform);
    }

    void frame(const glm::vec3& position, const bool started = false, const bool finished = false)
    {
      gizmo::Pose pose;
      pose.position = position;
      framePose(pose, started, finished);
    }
  };
}

TEST(GizmoEditSendingTest, ClickWithoutMovementSendsNothing)
{
  Rig rig;
  const auto start = rig.transform->getLocalPosition();

  rig.frame(start, true);
  rig.frame(start);
  rig.frame(start);
  rig.frame(start, false, true);

  EXPECT_TRUE(rig.perFrame.empty());
  EXPECT_TRUE(rig.commits.empty());

  // Positive control: the same sequence with a move does send.
  rig.frame(start, true);
  rig.frame(start + glm::vec3(1.0f, 0.0f, 0.0f));
  rig.frame(start + glm::vec3(1.0f, 0.0f, 0.0f), false, true);
  EXPECT_EQ(rig.perFrame.size(), 1u);
  EXPECT_EQ(rig.commits.size(), 1u);
}

TEST(GizmoEditSendingTest, DragSendsOneEditPerChangedFrameAndOneCommitOnRelease)
{
  Rig rig;
  const auto beforeJson = rig.transform->serialize();

  rig.frame(glm::vec3(1.0f, 0.0f, 0.0f), true);
  rig.frame(glm::vec3(2.0f, 0.0f, 0.0f));
  rig.frame(glm::vec3(3.0f, 0.0f, 0.0f), false, true);

  EXPECT_EQ(rig.perFrame.size(), 3u);
  ASSERT_EQ(rig.commits.size(), 1u);
  EXPECT_EQ(rig.commits[0].first, beforeJson);
  EXPECT_EQ(rig.commits[0].second, rig.transform->serialize());
  EXPECT_NE(rig.commits[0].first, rig.commits[0].second);
  EXPECT_EQ(rig.committedTargets[0], rig.target());
}

TEST(GizmoEditSendingTest, RepeatedPoseSendsNoFrameEditButStillCommitsOnce)
{
  Rig rig;

  rig.frame(glm::vec3(1.0f, 0.0f, 0.0f), true);
  rig.frame(glm::vec3(1.0f, 0.0f, 0.0f));
  rig.frame(glm::vec3(1.0f, 0.0f, 0.0f));
  rig.frame(glm::vec3(1.0f, 0.0f, 0.0f), false, true);

  EXPECT_EQ(rig.perFrame.size(), 1u);
  EXPECT_EQ(rig.commits.size(), 1u);
}

TEST(GizmoEditSendingTest, ReturningToTheStartPoseSendsFrameEditsButNoCommit)
{
  Rig rig;
  const auto start = rig.transform->getLocalPosition();

  rig.frame(start + glm::vec3(1.0f, 0.0f, 0.0f), true);
  rig.frame(start);
  rig.frame(start, false, true);

  EXPECT_EQ(rig.perFrame.size(), 2u);
  EXPECT_TRUE(rig.commits.empty());
}

TEST(GizmoEditSendingTest, StartAndFinishOnTheSameFrameSendsOneEditAndOneCommit)
{
  Rig rig;
  const auto beforeJson = rig.transform->serialize();

  rig.frame(glm::vec3(2.0f, 0.0f, 0.0f), true, true);

  EXPECT_EQ(rig.perFrame.size(), 1u);
  ASSERT_EQ(rig.commits.size(), 1u);
  EXPECT_EQ(rig.commits[0].first, beforeJson);
  EXPECT_EQ(rig.commits[0].second, rig.transform->serialize());
}

TEST(GizmoEditSendingTest, AbandoningAfterMovementCommitsOnce)
{
  Rig rig;
  const auto beforeJson = rig.transform->serialize();

  rig.frame(glm::vec3(0.0f, 4.0f, 0.0f), true);
  rig.sending.abandon(rig.transform);

  ASSERT_EQ(rig.commits.size(), 1u);
  EXPECT_EQ(rig.commits[0].first, beforeJson);
  EXPECT_EQ(rig.commits[0].second, rig.transform->serialize());
  EXPECT_FALSE(rig.sending.dragTarget().has_value());

  rig.sending.abandon(rig.transform);
  EXPECT_EQ(rig.commits.size(), 1u);
}

TEST(GizmoEditSendingTest, AbandoningWithoutMovementCommitsNothing)
{
  Rig rig;
  const auto start = rig.transform->getLocalPosition();

  rig.frame(start, true);
  rig.frame(start);
  ASSERT_TRUE(rig.sending.dragTarget().has_value());

  rig.sending.abandon(rig.transform);

  EXPECT_TRUE(rig.commits.empty());
  EXPECT_FALSE(rig.sending.dragTarget().has_value());
}

TEST(GizmoEditSendingTest, AbandoningWithTheTransformGoneCommitsNothing)
{
  Rig rig;

  rig.frame(glm::vec3(0.0f, 4.0f, 0.0f), true);
  ASSERT_EQ(rig.perFrame.size(), 1u);
  ASSERT_TRUE(rig.sending.dragTarget().has_value());

  rig.sending.abandon(nullptr);

  EXPECT_TRUE(rig.commits.empty());
  EXPECT_FALSE(rig.sending.dragTarget().has_value());
}

TEST(GizmoEditSendingTest, ARotationOnlyDragSendsFrameEditsAndACommit)
{
  Rig rig;
  const auto beforeJson = rig.transform->serialize();

  gizmo::Pose pose;
  pose.rotation = glm::vec3(0.0f, 30.0f, 0.0f);
  rig.framePose(pose, true);
  pose.rotation = glm::vec3(0.0f, 60.0f, 0.0f);
  rig.framePose(pose, false, true);

  EXPECT_EQ(rig.perFrame.size(), 2u);
  ASSERT_EQ(rig.commits.size(), 1u);
  EXPECT_EQ(rig.commits[0].first, beforeJson);
  EXPECT_EQ(rig.commits[0].second, rig.transform->serialize());
  EXPECT_NE(rig.commits[0].first, rig.commits[0].second);
  fixtures::expectNear(rig.transform->getLocalRotation(), glm::vec3(0.0f, 60.0f, 0.0f));
  fixtures::expectNear(rig.transform->getLocalPosition(), glm::vec3(0.0f));
}

TEST(GizmoEditSendingTest, AScaleOnlyDragSendsFrameEditsAndACommit)
{
  Rig rig;
  const auto beforeJson = rig.transform->serialize();

  gizmo::Pose pose;
  pose.scale = glm::vec3(1.5f, 1.0f, 1.0f);
  rig.framePose(pose, true);
  pose.scale = glm::vec3(2.0f, 1.0f, 1.0f);
  rig.framePose(pose, false, true);

  EXPECT_EQ(rig.perFrame.size(), 2u);
  ASSERT_EQ(rig.commits.size(), 1u);
  EXPECT_EQ(rig.commits[0].first, beforeJson);
  EXPECT_EQ(rig.commits[0].second, rig.transform->serialize());
  EXPECT_NE(rig.commits[0].first, rig.commits[0].second);
  fixtures::expectNear(rig.transform->getLocalScale(), glm::vec3(2.0f, 1.0f, 1.0f));
  fixtures::expectNear(rig.transform->getLocalPosition(), glm::vec3(0.0f));
}

TEST(GizmoEditSendingTest, FrameEditsCarryTheTargetAndTheTransformComponent)
{
  Rig rig;

  rig.frame(glm::vec3(1.0f, 0.0f, 0.0f), true);
  rig.frame(glm::vec3(2.0f, 0.0f, 0.0f), false, true);

  ASSERT_EQ(rig.frameTargets.size(), 2u);
  ASSERT_EQ(rig.frameComponents.size(), 2u);
  for (size_t i = 0; i < rig.frameTargets.size(); ++i)
  {
    EXPECT_EQ(rig.frameTargets[i], rig.target());
    EXPECT_EQ(rig.frameComponents[i], rig.transform.get());
  }
}
