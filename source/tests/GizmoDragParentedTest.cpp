#include <gtest/gtest.h>

#include "Gizmo.h"
#include "GizmoFixtures.h"
#include "TestScene.h"

#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace {
  using namespace gizmoFixtures;

  [[nodiscard]] glm::vec3 composeWorldPosition(const gizmo::ParentFrame& parent, const glm::vec3& local)
  {
    return parent.position + parent.orientation * (parent.scale * local);
  }

  [[nodiscard]] glm::quat composeWorldOrientation(const gizmo::ParentFrame& parent, const glm::vec3& localEuler)
  {
    return parent.orientation * glm::quat(glm::radians(localEuler));
  }

  void placeChild(GizmoFixture& f, const gizmo::ParentFrame& parent, const gizmo::Pose& local)
  {
    f.input.parent = parent;
    f.input.local = local;
    f.input.world.position = composeWorldPosition(parent, local.position);
    f.input.world.rotation = glm::degrees(glm::eulerAngles(composeWorldOrientation(parent, local.rotation)));
    f.input.world.scale = parent.scale * local.scale;
  }

  // Grabs the world X arrow of a child under `parent`, drags it 50 pixels right, and returns the frame.
  [[nodiscard]] gizmo::Frame dragWorldXArrow(GizmoFixture& f, const glm::vec2& handleScreenPoint)
  {
    EXPECT_TRUE(beginDragOnHandle(f, handleScreenPoint).dragStarted);
    f.input.mouse = handleScreenPoint + glm::vec2(50.0f, 0.0f);
    return gizmo::update(f.state, f.input);
  }
}

TEST(GizmoDragTest, TranslateUnderARotatedParentMovesTheChildAlongWorldX)
{
  gizmo::ParentFrame parent;
  parent.orientation = glm::quat(glm::radians(glm::vec3(0.0f, 90.0f, 0.0f)));
  parent.scale = glm::vec3(2.0f);

  GizmoFixture f;
  f.state.mode = gizmo::Mode::translate;
  gizmo::Pose local;
  local.position = glm::vec3(1.0f, 0.0f, 0.0f);
  placeChild(f, parent, local);

  const glm::vec3 startWorld = f.input.world.position;
  fixtures::expectNear(startWorld, glm::vec3(0.0f, 0.0f, -2.0f), 1e-4f);

  const auto initial = gizmo::update(f.state, f.input);
  const glm::vec2 xTipScreen = *gizmo::project(f.view, findLine(initial, gizmo::Handle::x)->b);
  const glm::vec2 grabOffset = xTipScreen - *gizmo::project(f.view, startWorld);

  const auto frame = dragWorldXArrow(f, xTipScreen);
  ASSERT_TRUE(frame.local.has_value());

  const glm::vec3 endWorld = composeWorldPosition(parent, frame.local->position);
  EXPECT_GT(endWorld.x, startWorld.x + 0.01f);
  EXPECT_NEAR(endWorld.y, startWorld.y, 1e-4f);
  EXPECT_NEAR(endWorld.z, startWorld.z, 1e-4f);

  const auto projectedBack = gizmo::project(f.view, endWorld);
  ASSERT_TRUE(projectedBack.has_value());
  EXPECT_NEAR(projectedBack->x, xTipScreen.x + 50.0f - grabOffset.x, 0.5f);
}

TEST(GizmoDragTest, TranslateUnderAScaledParentDividesTheWorldDeltaByTheScale)
{
  gizmo::ParentFrame parent;
  parent.position = glm::vec3(1.0f, 0.0f, 0.0f);
  parent.scale = glm::vec3(2.0f);

  GizmoFixture f;
  f.state.mode = gizmo::Mode::translate;
  gizmo::Pose local;
  local.position = glm::vec3(0.5f, 0.0f, 0.0f);
  placeChild(f, parent, local);

  const glm::vec3 startWorld = f.input.world.position;
  const auto initial = gizmo::update(f.state, f.input);
  const glm::vec2 xTipScreen = *gizmo::project(f.view, findLine(initial, gizmo::Handle::x)->b);

  const auto frame = dragWorldXArrow(f, xTipScreen);
  ASSERT_TRUE(frame.local.has_value());

  const float worldDelta = composeWorldPosition(parent, frame.local->position).x - startWorld.x;
  EXPECT_GT(worldDelta, 0.01f);
  EXPECT_NEAR(frame.local->position.x - 0.5f, worldDelta / 2.0f, 1e-4f);
  EXPECT_NEAR(frame.local->position.y, 0.0f, 1e-5f);
}

TEST(GizmoDragTest, TranslateKeepsTheLocalComponentWhereTheParentScaleIsZero)
{
  gizmo::ParentFrame parent;
  parent.scale = glm::vec3(0.0f, 1.0f, 1.0f);

  GizmoFixture f;
  f.state.mode = gizmo::Mode::translate;
  gizmo::Pose local;
  local.position = glm::vec3(3.0f, 0.0f, 0.0f);
  placeChild(f, parent, local);

  const auto initial = gizmo::update(f.state, f.input);
  const glm::vec2 xTipScreen = *gizmo::project(f.view, findLine(initial, gizmo::Handle::x)->b);

  const auto frame = dragWorldXArrow(f, xTipScreen);
  ASSERT_TRUE(frame.local.has_value());
  EXPECT_EQ(frame.local->position.x, 3.0f);
  EXPECT_TRUE(std::isfinite(frame.local->position.y));
  EXPECT_TRUE(std::isfinite(frame.local->position.z));

  // Positive control: with a nonzero parent scale the same drag does move that component.
  gizmo::ParentFrame scaled;
  scaled.scale = glm::vec3(2.0f, 1.0f, 1.0f);
  GizmoFixture g;
  g.state.mode = gizmo::Mode::translate;
  placeChild(g, scaled, local);

  const auto initialG = gizmo::update(g.state, g.input);
  const glm::vec2 tipG = *gizmo::project(g.view, findLine(initialG, gizmo::Handle::x)->b);
  const auto frameG = dragWorldXArrow(g, tipG);

  ASSERT_TRUE(frameG.local.has_value());
  EXPECT_GT(std::abs(frameG.local->position.x - 3.0f), 0.01f);
}

TEST(GizmoDragTest, RotateUnderARotatedParentTurnsTheChildAboutTheWorldAxis)
{
  gizmo::ParentFrame parent;
  parent.orientation = glm::quat(glm::radians(glm::vec3(20.0f, 50.0f, 30.0f)));

  GizmoFixture f;
  f.state.mode = gizmo::Mode::rotate;
  gizmo::Pose local;
  local.rotation = glm::vec3(10.0f, 0.0f, 0.0f);
  placeChild(f, parent, local);

  const glm::quat startWorld = composeWorldOrientation(parent, local.rotation);
  const glm::vec2 centerScreen = *gizmo::project(f.view, f.input.world.position);

  const auto pressFrame = beginDragOnHandle(f, ringPointScreen(centerScreen, 0.0f));
  ASSERT_EQ(pressFrame.active, gizmo::Handle::z);

  gizmo::Frame frame;
  constexpr int steps = 12;
  for (int i = 1; i <= steps; ++i)
  {
    const float angle = glm::radians(90.0f) * static_cast<float>(i) / static_cast<float>(steps);
    f.input.mouse = ringPointScreen(centerScreen, angle);
    frame = gizmo::update(f.state, f.input);
  }

  ASSERT_TRUE(frame.local.has_value());
  const glm::quat endWorld = composeWorldOrientation(parent, frame.local->rotation);
  const glm::quat relative = endWorld * glm::inverse(startWorld);

  EXPECT_NEAR(rotationAngleDegrees(relative), 90.0f, 2.0f);
  // A turn about world Z has its axis on Z: |z| of the unit quaternion is sin(45 degrees).
  EXPECT_NEAR(std::abs(relative.z), std::sin(glm::radians(45.0f)), 0.02f);
  EXPECT_NEAR(relative.x, 0.0f, 0.02f);
  EXPECT_NEAR(relative.y, 0.0f, 0.02f);
}

TEST(GizmoDragTest, TranslateOfARootObjectAddsTheWorldDeltaToLocalExactly)
{
  GizmoFixture f;
  f.state.mode = gizmo::Mode::translate;
  gizmo::Pose local;
  local.position = glm::vec3(0.25f, 1.5f, -0.75f);
  placeChild(f, gizmo::ParentFrame{}, local);

  const auto initial = gizmo::update(f.state, f.input);
  const glm::vec2 xTipScreen = *gizmo::project(f.view, findLine(initial, gizmo::Handle::x)->b);

  const auto frame = dragWorldXArrow(f, xTipScreen);
  ASSERT_TRUE(frame.local.has_value());

  EXPECT_NE(f.state.dragLastAmount, 0.0f);
  EXPECT_EQ(frame.local->position, local.position + f.state.dragAxis * f.state.dragLastAmount);
}

TEST(GizmoDragTest, PressingARotateHandleUnderARotatedParentLeavesLocalRotationUntouched)
{
  gizmo::ParentFrame parent;
  parent.orientation = glm::quat(glm::radians(glm::vec3(20.0f, 50.0f, 30.0f)));

  GizmoFixture f;
  f.state.mode = gizmo::Mode::rotate;
  gizmo::Pose local;
  local.rotation = glm::vec3(10.0f, 25.0f, -40.0f);
  placeChild(f, parent, local);

  const glm::vec2 centerScreen = *gizmo::project(f.view, f.input.world.position);
  const auto pressFrame = beginDragOnHandle(f, ringPointScreen(centerScreen, 0.0f));
  ASSERT_TRUE(pressFrame.dragStarted);
  ASSERT_TRUE(pressFrame.local.has_value());
  EXPECT_EQ(pressFrame.local->rotation, local.rotation);

  // Positive control: a real sweep does change the rotation.
  f.input.mouse = ringPointScreen(centerScreen, glm::radians(60.0f));
  const auto moved = gizmo::update(f.state, f.input);
  ASSERT_TRUE(moved.local.has_value());
  EXPECT_NE(moved.local->rotation, local.rotation);
}

TEST(GizmoDragTest, ADragKeepsTheParentFrameCapturedAtItsStart)
{
  gizmo::ParentFrame parent;
  parent.scale = glm::vec3(2.0f);

  GizmoFixture f;
  f.state.mode = gizmo::Mode::translate;
  gizmo::Pose local;
  local.position = glm::vec3(0.5f, 0.0f, 0.0f);
  placeChild(f, parent, local);

  const glm::vec3 startWorld = f.input.world.position;
  const auto initial = gizmo::update(f.state, f.input);
  const glm::vec2 xTipScreen = *gizmo::project(f.view, findLine(initial, gizmo::Handle::x)->b);

  ASSERT_TRUE(beginDragOnHandle(f, xTipScreen).dragStarted);

  f.input.parent.scale = glm::vec3(10.0f);
  f.input.parent.position = glm::vec3(5.0f, 0.0f, 0.0f);
  f.input.mouse = xTipScreen + glm::vec2(50.0f, 0.0f);
  const auto frame = gizmo::update(f.state, f.input);

  ASSERT_TRUE(frame.local.has_value());
  const float worldDelta = composeWorldPosition(parent, frame.local->position).x - startWorld.x;
  EXPECT_GT(worldDelta, 0.01f);
  EXPECT_NEAR(frame.local->position.x - 0.5f, worldDelta / 2.0f, 1e-4f);
}

TEST(GizmoDragTest, TranslateUnderATranslatedAndRotatedParentMovesTheChildAlongWorldX)
{
  gizmo::ParentFrame parent;
  parent.position = glm::vec3(3.0f, -1.0f, 2.0f);
  parent.orientation = glm::quat(glm::radians(glm::vec3(0.0f, 90.0f, 0.0f)));

  GizmoFixture f;
  f.state.mode = gizmo::Mode::translate;
  gizmo::Pose local;
  local.position = glm::vec3(1.0f, 0.5f, 0.0f);
  placeChild(f, parent, local);

  const glm::vec3 startWorld = f.input.world.position;
  const auto initial = gizmo::update(f.state, f.input);
  const glm::vec2 xTipScreen = *gizmo::project(f.view, findLine(initial, gizmo::Handle::x)->b);

  const auto frame = dragWorldXArrow(f, xTipScreen);
  ASSERT_TRUE(frame.local.has_value());

  const glm::vec3 endWorld = composeWorldPosition(parent, frame.local->position);
  EXPECT_GT(endWorld.x, startWorld.x + 0.01f);
  EXPECT_NEAR(endWorld.y, startWorld.y, 1e-4f);
  EXPECT_NEAR(endWorld.z, startWorld.z, 1e-4f);
}
