#include <gtest/gtest.h>

#include "Gizmo.h"
#include "GizmoFixtures.h"
#include "TestScene.h"

namespace {
  using namespace gizmoFixtures;
}

TEST(GizmoTest, ProjectsThePointsWeExpect)
{
  GizmoFixture f;

  const auto center = gizmo::project(f.view, glm::vec3(0.0f));
  ASSERT_TRUE(center.has_value());
  EXPECT_NEAR(center->x, 400.0f, 0.5f);
  EXPECT_NEAR(center->y, 300.0f, 0.5f);

  const auto behind = gizmo::project(f.view, glm::vec3(0.0f, 0.0f, 20.0f));
  EXPECT_FALSE(behind.has_value());

  const auto right = gizmo::project(f.view, glm::vec3(1.0f, 0.0f, 0.0f));
  ASSERT_TRUE(right.has_value());
  EXPECT_GT(right->x, center->x);

  const auto up = gizmo::project(f.view, glm::vec3(0.0f, 1.0f, 0.0f));
  ASSERT_TRUE(up.has_value());
  EXPECT_LT(up->y, center->y);
}

TEST(GizmoTest, MouseRayThroughTheCenterPointsAtTheOrigin)
{
  GizmoFixture f;

  const auto ray = gizmo::mouseRay(f.view, glm::vec2(400.0f, 300.0f));

  fixtures::expectNear(ray.direction, glm::vec3(0.0f, 0.0f, -1.0f), 1e-3f);
}

TEST(GizmoTest, MouseRayRoundTripsThroughProject)
{
  GizmoFixture f;
  const glm::vec2 pixel(550.0f, 200.0f);

  const auto ray = gizmo::mouseRay(f.view, pixel);
  const auto projected = gizmo::project(f.view, ray.origin + ray.direction * 10.0f);

  ASSERT_TRUE(projected.has_value());
  EXPECT_NEAR(projected->x, pixel.x, 0.5f);
  EXPECT_NEAR(projected->y, pixel.y, 0.5f);
}

TEST(GizmoTest, ProjectAndMouseRayHonorTheViewportOrigin)
{
  GizmoFixture f;
  f.view.viewport = gizmo::Rect{ 300.0f, 200.0f, 800.0f, 600.0f };

  const auto center = gizmo::project(f.view, glm::vec3(0.0f));
  ASSERT_TRUE(center.has_value());
  EXPECT_NEAR(center->x, 700.0f, 0.5f);
  EXPECT_NEAR(center->y, 500.0f, 0.5f);

  const auto ray = gizmo::mouseRay(f.view, glm::vec2(700.0f, 500.0f));
  fixtures::expectNear(ray.direction, glm::vec3(0.0f, 0.0f, -1.0f), 1e-3f);

  const glm::vec2 pixel(850.0f, 400.0f);
  const auto offCenter = gizmo::mouseRay(f.view, pixel);
  const auto projected = gizmo::project(f.view, offCenter.origin + offCenter.direction * 10.0f);
  ASSERT_TRUE(projected.has_value());
  EXPECT_NEAR(projected->x, pixel.x, 0.5f);
  EXPECT_NEAR(projected->y, pixel.y, 0.5f);
}

TEST(GizmoTest, HandleScreenLengthIsConstantAcrossDistance)
{
  const auto measure = [](const float distance) {
    GizmoFixture f;
    f.view.view = glm::lookAt(glm::vec3(0.0f, 0.0f, distance), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    f.input.view = f.view;
    f.state.mode = gizmo::Mode::translate;

    const auto frame = gizmo::update(f.state, f.input);
    const auto* line = findLine(frame, gizmo::Handle::x);
    if (!line)
    {
      return -1.0f;
    }

    const auto pa = gizmo::project(f.view, line->a);
    const auto pb = gizmo::project(f.view, line->b);
    return (pa && pb) ? glm::length(*pb - *pa) : -1.0f;
  };

  EXPECT_NEAR(measure(10.0f), 100.0f, 1.0f);
  EXPECT_NEAR(measure(40.0f), 100.0f, 1.0f);
}

TEST(GizmoTest, HoverFindsTheNearestHandle)
{
  GizmoFixture f;
  f.state.mode = gizmo::Mode::translate;

  const auto initial = gizmo::update(f.state, f.input);
  const glm::vec3 xTip = findLine(initial, gizmo::Handle::x)->b;
  const glm::vec3 yTip = findLine(initial, gizmo::Handle::y)->b;

  f.input.mouse = *gizmo::project(f.view, xTip);
  EXPECT_EQ(gizmo::update(f.state, f.input).hovered, gizmo::Handle::x);

  f.input.mouse = *gizmo::project(f.view, yTip);
  EXPECT_EQ(gizmo::update(f.state, f.input).hovered, gizmo::Handle::y);

  // Positive control above found a handle; far from any of them finds none.
  f.input.mouse = glm::vec2(10.0f, 10.0f);
  EXPECT_EQ(gizmo::update(f.state, f.input).hovered, gizmo::Handle::none);

  f.input.mouse = *gizmo::project(f.view, xTip);
  f.input.mouseOverView = false;
  EXPECT_EQ(gizmo::update(f.state, f.input).hovered, gizmo::Handle::none);
}

TEST(GizmoTest, ShaftHoverReachesSevenPixelsButNotNine)
{
  GizmoFixture f;
  f.state.mode = gizmo::Mode::translate;

  const auto initial = gizmo::update(f.state, f.input);
  const gizmo::Line* shaft = findLine(initial, gizmo::Handle::x);
  ASSERT_NE(shaft, nullptr);
  const glm::vec2 midpoint = (*gizmo::project(f.view, shaft->a) + *gizmo::project(f.view, shaft->b)) * 0.5f;

  f.input.mouse = midpoint + glm::vec2(0.0f, 7.0f);
  EXPECT_EQ(gizmo::update(f.state, f.input).hovered, gizmo::Handle::x);

  f.input.mouse = midpoint + glm::vec2(0.0f, 9.0f);
  EXPECT_EQ(gizmo::update(f.state, f.input).hovered, gizmo::Handle::none);
}

TEST(GizmoTest, UniformHandleHoverReachesFivePixelsButNotSeven)
{
  GizmoFixture f;
  f.state.mode = gizmo::Mode::scale;
  const glm::vec2 center = *gizmo::project(f.view, glm::vec3(0.0f));

  // Offsets toward the lower left keep the X and Y shafts out of range, so only the uniform box decides.
  f.input.mouse = center + glm::vec2(-5.0f, 5.0f);
  EXPECT_EQ(gizmo::update(f.state, f.input).hovered, gizmo::Handle::uniform);

  f.input.mouse = center + glm::vec2(-7.0f, 7.0f);
  EXPECT_EQ(gizmo::update(f.state, f.input).hovered, gizmo::Handle::none);
}

TEST(GizmoTest, AnEdgeOnRingTieGoesToTheFaceOnRing)
{
  GizmoFixture f;
  f.state.mode = gizmo::Mode::rotate;
  const glm::vec2 center = *gizmo::project(f.view, glm::vec3(0.0f));

  // The X ring is edge-on here (a vertical line through the center) and the Z ring is face-on; the top of
  // the Z circle lies on that line. Positive control first: below the Z circle only the X ring is in reach.
  f.input.mouse = center + glm::vec2(0.0f, 50.0f);
  EXPECT_EQ(gizmo::update(f.state, f.input).hovered, gizmo::Handle::x);

  // Here the X ring is 0.3 px closer than the Z ring, inside the tie epsilon, so reliability decides.
  f.input.mouse = center + glm::vec2(0.0f, -99.7f);
  EXPECT_EQ(gizmo::update(f.state, f.input).hovered, gizmo::Handle::z);

  // Two pixels inside the Z circle the X ring is clearly nearer, outside the tie epsilon, so distance wins.
  f.input.mouse = center + glm::vec2(0.0f, -98.0f);
  EXPECT_EQ(gizmo::update(f.state, f.input).hovered, gizmo::Handle::x);
}

TEST(GizmoTest, DegenerateViewportEmitsNothingAndNeverHovers)
{
  GizmoFixture f;
  f.state.mode = gizmo::Mode::translate;

  // Positive control: an ordinary viewport does emit primitives.
  EXPECT_FALSE(gizmo::update(f.state, f.input).triangles.empty());

  f.view.viewport.width = 0.0f;
  f.input.view = f.view;
  f.input.mouse = glm::vec2(1.0f, 1.0f);

  const auto frame = gizmo::update(f.state, f.input);
  EXPECT_TRUE(frame.triangles.empty());
  EXPECT_TRUE(frame.lines.empty());
  EXPECT_EQ(frame.hovered, gizmo::Handle::none);
}

TEST(GizmoTest, TargetBehindCameraEmitsNothingAndNeverHovers)
{
  GizmoFixture f;
  f.state.mode = gizmo::Mode::translate;

  // Positive control: the target in front of the camera emits primitives.
  EXPECT_FALSE(gizmo::update(f.state, f.input).triangles.empty());

  f.input.world.position = glm::vec3(0.0f, 0.0f, 20.0f);
  f.input.local.position = f.input.world.position;
  f.input.mouse = glm::vec2(400.0f, 300.0f);

  const auto frame = gizmo::update(f.state, f.input);
  EXPECT_TRUE(frame.triangles.empty());
  EXPECT_TRUE(frame.lines.empty());
  EXPECT_EQ(frame.hovered, gizmo::Handle::none);
}

TEST(GizmoTest, TranslatePrimitiveCountsMatchThreeShaftsAndTips)
{
  auto f = makeOffAxisFixture();
  f.state.mode = gizmo::Mode::translate;

  const auto frame = gizmo::update(f.state, f.input);

  // 3 tips (1 triangle each); the shafts are 3 thick lines, not triangles.
  EXPECT_EQ(frame.triangles.size(), 3u);
  EXPECT_EQ(frame.lines.size(), 3u);
}

TEST(GizmoTest, ShaftsAreThickLinesNotThinTriangles)
{
  auto f = makeOffAxisFixture();

  for (const auto mode : { gizmo::Mode::translate, gizmo::Mode::scale })
  {
    f.state.mode = mode;
    const auto frame = gizmo::update(f.state, f.input);

    ASSERT_EQ(frame.lines.size(), 3u);
    for (const auto& line : frame.lines)
    {
      EXPECT_GT(line.thicknessPixels, 1.0f);
    }

    // A thin full-length triangle makes ImGui's anti-aliasing spike past the shaft ends.
    for (const auto& tri : frame.triangles)
    {
      if (tri.handle == gizmo::Handle::uniform)
      {
        continue;
      }

      const auto* line = findLine(frame, tri.handle);
      ASSERT_NE(line, nullptr);
      const glm::vec2 pa = *gizmo::project(f.view, line->a);
      const glm::vec2 pb = *gizmo::project(f.view, line->b);
      const glm::vec2 centroid = (*gizmo::project(f.view, tri.a) + *gizmo::project(f.view, tri.b) +
                                  *gizmo::project(f.view, tri.c)) / 3.0f;
      EXPECT_LT(glm::length(centroid - pb), glm::length(pb - pa) * 0.5f);
    }
  }
}

TEST(GizmoTest, HoveredHandlePrimitivesCarryTheHoveredHighlight)
{
  auto f = makeOffAxisFixture();
  f.state.mode = gizmo::Mode::translate;

  const auto initial = gizmo::update(f.state, f.input);
  f.input.mouse = *gizmo::project(f.view, findLine(initial, gizmo::Handle::x)->b);

  const auto frame = gizmo::update(f.state, f.input);
  ASSERT_EQ(frame.hovered, gizmo::Handle::x);

  bool foundHighlighted = false;
  for (const auto& tri : frame.triangles)
  {
    if (tri.handle == gizmo::Handle::x)
    {
      EXPECT_EQ(tri.highlight, gizmo::Highlight::hovered);
      foundHighlighted = true;
    }
    else
    {
      EXPECT_EQ(tri.highlight, gizmo::Highlight::none);
    }
  }
  EXPECT_TRUE(foundHighlighted);
}

TEST(GizmoTest, EveryTranslatePrimitiveVertexProjectsNearItsHandlesHoverGeometry)
{
  auto f = makeOffAxisFixture();
  f.state.mode = gizmo::Mode::translate;

  const auto frame = gizmo::update(f.state, f.input);

  for (const auto& tri : frame.triangles)
  {
    const auto* line = findLine(frame, tri.handle);
    ASSERT_NE(line, nullptr);

    const glm::vec2 pa = *gizmo::project(f.view, line->a);
    const glm::vec2 pb = *gizmo::project(f.view, line->b);

    for (const glm::vec3& v : { tri.a, tri.b, tri.c })
    {
      const auto pv = gizmo::project(f.view, v);
      ASSERT_TRUE(pv.has_value());
      EXPECT_LT(pointSegmentDistance(*pv, pa, pb), 8.0f);
    }
  }
}
