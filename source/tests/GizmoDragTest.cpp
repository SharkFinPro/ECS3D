#include <gtest/gtest.h>

#include "Gizmo.h"
#include "GizmoFixtures.h"
#include "TestScene.h"

#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace {
  using namespace gizmoFixtures;
}

TEST(GizmoDragTest, TranslateAlongXMatchesTheScreenMovement)
{
  GizmoFixture f;
  f.state.mode = gizmo::Mode::translate;

  const auto initial = gizmo::update(f.state, f.input);
  const glm::vec3 xTip = findLine(initial, gizmo::Handle::x)->b;
  const glm::vec2 xTipScreen = *gizmo::project(f.view, xTip);
  const glm::vec2 centerScreenAtStart = *gizmo::project(f.view, glm::vec3(0.0f));
  // The handle was grabbed here, not at the center, so it is this point - not the object's origin - that
  // tracks the cursor; the center trails it by however far it started from the center on screen.
  const glm::vec2 grabOffset = xTipScreen - centerScreenAtStart;

  const auto pressFrame = beginDragOnHandle(f, xTipScreen);
  EXPECT_TRUE(pressFrame.dragStarted);

  const glm::vec2 moveTo = xTipScreen + glm::vec2(50.0f, 0.0f);
  f.input.mouse = moveTo;
  const auto frame = gizmo::update(f.state, f.input);

  ASSERT_TRUE(frame.local.has_value());
  EXPECT_NEAR(frame.local->position.y, 0.0f, 1e-4f);
  EXPECT_NEAR(frame.local->position.z, 0.0f, 1e-4f);
  EXPECT_GT(frame.local->position.x, 0.0f);

  const auto projectedBack = gizmo::project(f.view, frame.local->position);
  ASSERT_TRUE(projectedBack.has_value());
  EXPECT_NEAR(projectedBack->x, moveTo.x - grabOffset.x, 0.5f);

  f.input.mouseDown = false;
  EXPECT_TRUE(gizmo::update(f.state, f.input).dragFinished);
}

TEST(GizmoDragTest, TranslateSnapsToTheConfiguredStep)
{
  GizmoFixture f;
  f.state.mode = gizmo::Mode::translate;
  f.input.snap.enabled = true;

  const auto initial = gizmo::update(f.state, f.input);
  const glm::vec2 xTipScreen = *gizmo::project(f.view, findLine(initial, gizmo::Handle::x)->b);
  beginDragOnHandle(f, xTipScreen);

  f.input.mouse = xTipScreen + glm::vec2(37.0f, 0.0f);
  const auto frame = gizmo::update(f.state, f.input);

  ASSERT_TRUE(frame.local.has_value());
  const float amount = frame.local->position.x;
  EXPECT_NEAR(std::round(amount / 0.5f) * 0.5f, amount, 1e-4f);
}

TEST(GizmoDragTest, SuppressSnapLeavesTheMoveUnsnapped)
{
  // Positive control: the same drag with snap enabled and not suppressed does land on the grid, so the
  // non-multiple checked below is actually suppressSnap's doing.
  {
    GizmoFixture snapped;
    snapped.state.mode = gizmo::Mode::translate;
    snapped.input.snap.enabled = true;

    const auto initial = gizmo::update(snapped.state, snapped.input);
    const glm::vec2 xTipScreen = *gizmo::project(snapped.view, findLine(initial, gizmo::Handle::x)->b);
    beginDragOnHandle(snapped, xTipScreen);

    snapped.input.mouse = xTipScreen + glm::vec2(37.0f, 0.0f);
    const auto frame = gizmo::update(snapped.state, snapped.input);

    ASSERT_TRUE(frame.local.has_value());
    EXPECT_NEAR(std::round(frame.local->position.x / 0.5f) * 0.5f, frame.local->position.x, 1e-4f);
  }

  GizmoFixture f;
  f.state.mode = gizmo::Mode::translate;
  f.input.snap.enabled = true;
  f.input.suppressSnap = true;

  const auto initial = gizmo::update(f.state, f.input);
  const glm::vec2 xTipScreen = *gizmo::project(f.view, findLine(initial, gizmo::Handle::x)->b);
  beginDragOnHandle(f, xTipScreen);

  f.input.mouse = xTipScreen + glm::vec2(37.0f, 0.0f);
  const auto frame = gizmo::update(f.state, f.input);

  ASSERT_TRUE(frame.local.has_value());
  const float amount = frame.local->position.x;
  EXPECT_GT(std::abs(std::round(amount / 0.5f) * 0.5f - amount), 0.02f);
}

TEST(GizmoDragTest, TranslateOnAParentedObjectMovesLocalByTheWorldDelta)
{
  GizmoFixture f;
  f.state.mode = gizmo::Mode::translate;
  f.input.world.position = glm::vec3(2.0f, 0.0f, 0.0f);
  f.input.local.position = glm::vec3(0.5f, 0.0f, 0.0f);
  ASSERT_NE(f.input.local.position.x, f.input.world.position.x);

  const auto initial = gizmo::update(f.state, f.input);
  const glm::vec2 xTipScreen = *gizmo::project(f.view, findLine(initial, gizmo::Handle::x)->b);
  const glm::vec2 centerScreenAtStart = *gizmo::project(f.view, f.input.world.position);
  // See TranslateAlongXMatchesTheScreenMovement: the grabbed point, not the object's origin, tracks the
  // cursor.
  const glm::vec2 grabOffset = xTipScreen - centerScreenAtStart;

  beginDragOnHandle(f, xTipScreen);

  const glm::vec2 moveTo = xTipScreen + glm::vec2(50.0f, 0.0f);
  f.input.mouse = moveTo;
  const auto frame = gizmo::update(f.state, f.input);

  ASSERT_TRUE(frame.local.has_value());
  const float localDelta = frame.local->position.x - 0.5f;
  EXPECT_GT(std::abs(localDelta), 1e-4f);

  const glm::vec3 reconstructedWorld = f.input.world.position + glm::vec3(localDelta, 0.0f, 0.0f);
  const auto projectedBack = gizmo::project(f.view, reconstructedWorld);
  ASSERT_TRUE(projectedBack.has_value());
  EXPECT_NEAR(projectedBack->x, moveTo.x - grabOffset.x, 0.5f);
}

TEST(GizmoDragTest, FrameFlagsMarkExactlyThePressAndReleaseFrames)
{
  GizmoFixture f;
  f.state.mode = gizmo::Mode::translate;

  const auto initial = gizmo::update(f.state, f.input);
  const glm::vec2 xTipScreen = *gizmo::project(f.view, findLine(initial, gizmo::Handle::x)->b);

  f.input.mouse = xTipScreen;
  const auto hoverFrame = gizmo::update(f.state, f.input);
  EXPECT_FALSE(hoverFrame.dragStarted);
  EXPECT_TRUE(hoverFrame.capturesMouse);
  EXPECT_FALSE(hoverFrame.local.has_value());

  f.input.mouseDown = true;
  const auto pressFrame = gizmo::update(f.state, f.input);
  EXPECT_TRUE(pressFrame.dragStarted);
  EXPECT_FALSE(pressFrame.dragFinished);
  EXPECT_TRUE(pressFrame.capturesMouse);
  EXPECT_TRUE(pressFrame.local.has_value());

  f.input.mouse = xTipScreen + glm::vec2(20.0f, 0.0f);
  const auto dragFrame = gizmo::update(f.state, f.input);
  EXPECT_FALSE(dragFrame.dragStarted);
  EXPECT_FALSE(dragFrame.dragFinished);
  EXPECT_TRUE(dragFrame.capturesMouse);
  EXPECT_TRUE(dragFrame.local.has_value());

  f.input.mouseDown = false;
  const auto releaseFrame = gizmo::update(f.state, f.input);
  EXPECT_FALSE(releaseFrame.dragStarted);
  EXPECT_TRUE(releaseFrame.dragFinished);
  EXPECT_TRUE(releaseFrame.capturesMouse);
  EXPECT_TRUE(releaseFrame.local.has_value());

  const auto idleFrame = gizmo::update(f.state, f.input);
  EXPECT_FALSE(idleFrame.dragStarted);
  EXPECT_FALSE(idleFrame.dragFinished);
  EXPECT_FALSE(idleFrame.local.has_value());
}

TEST(GizmoDragTest, RotateAboutZSweepsToAQuarterTurn)
{
  GizmoFixture f;
  f.state.mode = gizmo::Mode::rotate;
  const glm::vec2 centerScreen = *gizmo::project(f.view, glm::vec3(0.0f));

  const auto hoverFrame = beginDragOnHandle(f, ringPointScreen(centerScreen, 0.0f));
  ASSERT_TRUE(hoverFrame.dragStarted);
  ASSERT_EQ(hoverFrame.active, gizmo::Handle::z);

  gizmo::Frame frame;
  constexpr int steps = 12;
  for (int i = 1; i <= steps; ++i)
  {
    const float angle = glm::radians(90.0f) * static_cast<float>(i) / static_cast<float>(steps);
    f.input.mouse = ringPointScreen(centerScreen, angle);
    frame = gizmo::update(f.state, f.input);
  }

  ASSERT_TRUE(frame.local.has_value());
  const glm::quat endQuat(glm::radians(frame.local->rotation));
  EXPECT_NEAR(rotationAngleDegrees(endQuat), 90.0f, 2.0f);
}

TEST(GizmoDragTest, RotateAccumulatesPastOneEightyDegrees)
{
  GizmoFixture f;
  f.state.mode = gizmo::Mode::rotate;
  const glm::vec2 centerScreen = *gizmo::project(f.view, glm::vec3(0.0f));

  const auto pressFrame = beginDragOnHandle(f, ringPointScreen(centerScreen, 0.0f));
  ASSERT_EQ(pressFrame.active, gizmo::Handle::z);

  constexpr int steps = 40;
  for (int i = 1; i <= steps; ++i)
  {
    const float angle = glm::radians(300.0f) * static_cast<float>(i) / static_cast<float>(steps);
    f.input.mouse = ringPointScreen(centerScreen, angle);
    gizmo::update(f.state, f.input);
  }

  EXPECT_GT(std::abs(f.state.dragTotalAngleDegrees), 180.0f);
}

TEST(GizmoDragTest, RotateSnapsToTheConfiguredStep)
{
  GizmoFixture f;
  f.state.mode = gizmo::Mode::rotate;
  f.input.snap.enabled = true;
  const glm::vec2 centerScreen = *gizmo::project(f.view, glm::vec3(0.0f));

  const auto pressFrame = beginDragOnHandle(f, ringPointScreen(centerScreen, 0.0f));
  ASSERT_EQ(pressFrame.active, gizmo::Handle::z);

  gizmo::Frame frame;
  constexpr int steps = 20;
  for (int i = 1; i <= steps; ++i)
  {
    const float angle = glm::radians(50.0f) * static_cast<float>(i) / static_cast<float>(steps);
    f.input.mouse = ringPointScreen(centerScreen, angle);
    frame = gizmo::update(f.state, f.input);
  }

  ASSERT_TRUE(frame.local.has_value());
  const float angleDeg = rotationAngleDegrees(glm::quat(glm::radians(frame.local->rotation)));

  EXPECT_NEAR(angleDeg, std::round(angleDeg / 15.0f) * 15.0f, 0.5f);
  // Positive control: the unsnapped sweep really was ~50 degrees, so the snap above actually moved it.
  EXPECT_GT(std::abs(angleDeg - 50.0f), 2.0f);
}

TEST(GizmoDragTest, RotateSignMatchesTheScreenSweepDirection)
{
  // The camera sits on +Z looking down -Z, so by the right-hand rule a positive rotation about +Z is the
  // one a viewer standing where the camera is would call counter-clockwise: +X turns toward +Y. Screen
  // space has y down, so tracing that counter-clockwise path (3 o'clock -> 12 o'clock -> 9 o'clock) means
  // the screen angle used to build ringPointScreen's offset must DECREASE (12 o'clock is up, i.e. a
  // smaller screen y, which sits at a negative angle in (cos, sin) terms) - hence sweeping with a
  // negative `angle` argument below.
  const auto sweepAndGetRotatedX = [](const float sweepDegrees) {
    GizmoFixture f;
    f.state.mode = gizmo::Mode::rotate;
    const glm::vec2 centerScreen = *gizmo::project(f.view, glm::vec3(0.0f));

    const auto pressFrame = beginDragOnHandle(f, ringPointScreen(centerScreen, 0.0f));
    if (pressFrame.active != gizmo::Handle::z)
    {
      return glm::vec3(std::numeric_limits<float>::quiet_NaN());
    }

    gizmo::Frame frame;
    constexpr int steps = 12;
    for (int i = 1; i <= steps; ++i)
    {
      const float angle = glm::radians(sweepDegrees) * static_cast<float>(i) / static_cast<float>(steps);
      f.input.mouse = ringPointScreen(centerScreen, angle);
      frame = gizmo::update(f.state, f.input);
    }

    if (!frame.local)
    {
      return glm::vec3(std::numeric_limits<float>::quiet_NaN());
    }

    const glm::quat q(glm::radians(frame.local->rotation));
    return q * glm::vec3(1.0f, 0.0f, 0.0f);
  };

  // Counter-clockwise on screen (negative parametric angle, see above) -> positive rotation about +Z ->
  // +X rotates toward +Y.
  const glm::vec3 ccw = sweepAndGetRotatedX(-90.0f);
  ASSERT_TRUE(std::isfinite(ccw.y));
  EXPECT_GT(ccw.y, 0.0f);

  // The mirror image: clockwise on screen gives the opposite sign.
  const glm::vec3 cw = sweepAndGetRotatedX(90.0f);
  ASSERT_TRUE(std::isfinite(cw.y));
  EXPECT_LT(cw.y, 0.0f);
}

TEST(GizmoDragTest, NearestEquivalentEulerMatchesTheBriefExample)
{
  const glm::vec3 result =
    gizmo::nearestEquivalentEuler(glm::vec3(180.0f, 10.0f, 180.0f), glm::vec3(0.0f, 170.0f, 0.0f));

  fixtures::expectNear(result, glm::vec3(0.0f, 170.0f, 0.0f), 1e-3f);
}

TEST(GizmoDragTest, NearestEquivalentEulerAlwaysRepresentsTheSameRotation)
{
  const std::vector<std::pair<glm::vec3, glm::vec3>> cases = {
    { glm::vec3(180.0f, 10.0f, 180.0f), glm::vec3(0.0f, 170.0f, 0.0f) },
    { glm::vec3(-170.0f, 91.0f, 5.0f), glm::vec3(190.0f, 89.0f, -175.0f) },
    { glm::vec3(20.0f, -95.0f, -10.0f), glm::vec3(-340.0f, 265.0f, 350.0f) },
  };

  for (const auto& [eulerDegrees, reference] : cases)
  {
    const glm::vec3 result = gizmo::nearestEquivalentEuler(eulerDegrees, reference);

    const glm::quat inputQuat(glm::radians(eulerDegrees));
    const glm::quat resultQuat(glm::radians(result));
    EXPECT_GT(std::abs(glm::dot(inputQuat, resultQuat)), 1.0f - 1e-4f);
  }
}

TEST(GizmoDragTest, ScaleAlongXHandleMatchesTheDraggedFraction)
{
  GizmoFixture f;
  f.state.mode = gizmo::Mode::scale;

  const auto initial = gizmo::update(f.state, f.input);
  const glm::vec2 xTipScreen = *gizmo::project(f.view, findLine(initial, gizmo::Handle::x)->b);
  const glm::vec2 centerScreen = *gizmo::project(f.view, glm::vec3(0.0f));

  beginDragOnHandle(f, xTipScreen);

  const glm::vec2 handleVector = xTipScreen - centerScreen;
  f.input.mouse = xTipScreen + handleVector;
  const auto frame = gizmo::update(f.state, f.input);

  ASSERT_TRUE(frame.local.has_value());
  EXPECT_NEAR(frame.local->scale.x, 2.0f, 0.1f);
  EXPECT_NEAR(frame.local->scale.y, 1.0f, 1e-4f);
  EXPECT_NEAR(frame.local->scale.z, 1.0f, 1e-4f);
}

TEST(GizmoDragTest, ScaleClampsAtTheMinimumFactorWhenDraggedInward)
{
  GizmoFixture f;
  f.state.mode = gizmo::Mode::scale;

  const auto initial = gizmo::update(f.state, f.input);
  const glm::vec2 xTipScreen = *gizmo::project(f.view, findLine(initial, gizmo::Handle::x)->b);
  const glm::vec2 centerScreen = *gizmo::project(f.view, glm::vec3(0.0f));

  beginDragOnHandle(f, xTipScreen);

  // Positive control: a moderate inward drag lands between the minimum and 1, so the clamp below is
  // actually clamping rather than always returning the minimum.
  f.input.mouse = centerScreen + (xTipScreen - centerScreen) * 0.5f;
  const auto moderateFrame = gizmo::update(f.state, f.input);
  ASSERT_TRUE(moderateFrame.local.has_value());
  EXPECT_GT(moderateFrame.local->scale.x, 0.1f);
  EXPECT_LT(moderateFrame.local->scale.x, 1.0f);

  f.input.mouse = centerScreen - (xTipScreen - centerScreen) * 5.0f;
  const auto frame = gizmo::update(f.state, f.input);

  ASSERT_TRUE(frame.local.has_value());
  EXPECT_NEAR(frame.local->scale.x, 0.01f, 1e-4f);
}

TEST(GizmoDragTest, UniformScaleHandleScalesAllThreeAxesEqually)
{
  GizmoFixture f;
  f.state.mode = gizmo::Mode::scale;
  const glm::vec2 centerScreen = *gizmo::project(f.view, glm::vec3(0.0f));

  const auto hoverFrame = beginDragOnHandle(f, centerScreen);
  ASSERT_TRUE(hoverFrame.dragStarted);

  const glm::vec2 dir = glm::normalize(glm::vec2(1.0f, -1.0f));
  f.input.mouse = centerScreen + dir * 100.0f;
  const auto frame = gizmo::update(f.state, f.input);

  ASSERT_TRUE(frame.local.has_value());
  EXPECT_NEAR(frame.local->scale.x, 2.0f, 0.1f);
  EXPECT_NEAR(frame.local->scale.y, 2.0f, 0.1f);
  EXPECT_NEAR(frame.local->scale.z, 2.0f, 0.1f);
}

TEST(GizmoDragTest, CancelAbandonsAnActiveDragWithNoResult)
{
  GizmoFixture f;
  f.state.mode = gizmo::Mode::translate;

  const auto initial = gizmo::update(f.state, f.input);
  const glm::vec2 xTipScreen = *gizmo::project(f.view, findLine(initial, gizmo::Handle::x)->b);
  const auto pressFrame = beginDragOnHandle(f, xTipScreen);
  ASSERT_TRUE(pressFrame.dragStarted);
  // Positive control: the drag really was live before cancel, so the absence checked below reflects
  // cancel() rather than the drag never having started.
  ASSERT_TRUE(pressFrame.local.has_value());

  gizmo::cancel(f.state);

  f.input.mouse = xTipScreen + glm::vec2(30.0f, 0.0f);
  const auto frame = gizmo::update(f.state, f.input);

  EXPECT_FALSE(frame.local.has_value());
  EXPECT_EQ(frame.active, gizmo::Handle::none);
}

TEST(GizmoDragTest, ChangingModeMidDragKeepsRunningTheDragItStartedWith)
{
  GizmoFixture f;
  f.state.mode = gizmo::Mode::translate;

  const auto initial = gizmo::update(f.state, f.input);
  const glm::vec2 xTipScreen = *gizmo::project(f.view, findLine(initial, gizmo::Handle::x)->b);
  const auto pressFrame = beginDragOnHandle(f, xTipScreen);
  ASSERT_TRUE(pressFrame.dragStarted);

  // A caller (e.g. a keybind handler) flips the mode mid-drag - the drag already in progress was set up
  // for translate (dragAxis, dragS0, ...) and must keep running that math to completion; the new mode
  // only takes effect on the next drag.
  f.state.mode = gizmo::Mode::rotate;

  f.input.mouse = xTipScreen + glm::vec2(50.0f, 0.0f);
  const auto dragFrame = gizmo::update(f.state, f.input);
  ASSERT_TRUE(dragFrame.local.has_value());
  EXPECT_GT(dragFrame.local->position.x, 0.0f);
  EXPECT_NEAR(dragFrame.local->rotation.x, 0.0f, 1e-4f);
  EXPECT_NEAR(dragFrame.local->rotation.y, 0.0f, 1e-4f);
  EXPECT_NEAR(dragFrame.local->rotation.z, 0.0f, 1e-4f);

  f.input.mouseDown = false;
  const auto releaseFrame = gizmo::update(f.state, f.input);
  EXPECT_TRUE(releaseFrame.dragFinished);
  ASSERT_TRUE(releaseFrame.local.has_value());
  // Positive control: the result really is a translation (not a no-op the rotation checks below would
  // pass vacuously).
  EXPECT_GT(releaseFrame.local->position.x, 0.0f);
  EXPECT_NEAR(releaseFrame.local->rotation.x, 0.0f, 1e-4f);
  EXPECT_NEAR(releaseFrame.local->rotation.y, 0.0f, 1e-4f);
  EXPECT_NEAR(releaseFrame.local->rotation.z, 0.0f, 1e-4f);
}
