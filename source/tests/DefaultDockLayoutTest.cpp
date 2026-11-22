#include <gtest/gtest.h>

#include <algorithm>
#include <array>

#include "DefaultDockLayout.h"
#include "DockLayoutFit.h"

namespace
{
constexpr std::array<std::array<float, 2>, 4> windowSizes = {{
  {1280.0f, 720.0f}, {1600.0f, 900.0f}, {1920.0f, 1080.0f}, {2560.0f, 1440.0f}
}};
constexpr std::array<float, 4> contentScales = {1.0f, 1.25f, 1.5f, 2.0f};

// One row of Scene Status controls: title bar, padding and a control row, at the base style sizes.
constexpr float sceneStatusBaseHeight = 60.0f;

constexpr float objectsFloor = 260.0f;
constexpr float inspectorFloor = 310.0f;
constexpr float assetsFloor = 200.0f;

float sideFloor(const float base, const float width, const float scale)
{
  return dockFloorForExtent(base, width / scale, dockSideFloorFraction) * scale;
}

float bottomFloor(const float base, const float height, const float scale)
{
  return dockFloorForExtent(base, height / scale, dockBottomFloorFraction) * scale;
}

// The sizes the engine ends up with: a default below its panel's floor is raised to the floor.
struct EffectiveLayout
{
  float left = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;
  float top = 0.0f;
};

EffectiveLayout effectiveLayout(const float width, const float height, const float scale)
{
  const DefaultDockSizes sizes = defaultDockSizes(width, height, scale);

  return {
    std::max(sizes.left, sideFloor(objectsFloor, width, scale)),
    std::max(sizes.right, sideFloor(inspectorFloor, width, scale)),
    std::max(sizes.bottom, bottomFloor(assetsFloor, height, scale)),
    sceneStatusBaseHeight * scale
  };
}
}

TEST(DefaultDockLayoutTest, SceneViewTakesTheMajorityOfEveryWindowAndScale)
{
  for (const auto& [width, height] : windowSizes)
  {
    for (const float scale : contentScales)
    {
      const EffectiveLayout layout = effectiveLayout(width, height, scale);

      EXPECT_GE(1.0f - (layout.left + layout.right) / width, 0.6f) << width << "x" << height << " @" << scale;
      EXPECT_GE(1.0f - (layout.top + layout.bottom) / height, 0.6f) << width << "x" << height << " @" << scale;
    }
  }
}

TEST(DefaultDockLayoutTest, DefaultsStayWithinTheirBudgets)
{
  for (const auto& [width, height] : windowSizes)
  {
    for (const float scale : contentScales)
    {
      const DefaultDockSizes sizes = defaultDockSizes(width, height, scale);

      EXPECT_LE(sizes.left + sizes.right, defaultDockSidesFraction * width + 0.01f) << width << "x" << height;
      EXPECT_LE(sizes.bottom, defaultDockBottomFraction * height + 0.01f) << width << "x" << height;
      EXPECT_LE(sizes.left, defaultDockLeftWidth * scale + 0.01f);
      EXPECT_LE(sizes.right, defaultDockRightWidth * scale + 0.01f);
      EXPECT_LE(sizes.bottom, defaultDockBottomHeight * scale + 0.01f);
    }
  }
}

TEST(DefaultDockLayoutTest, PanelsKeepTheirContentSizeWhenTheWindowHasRoom)
{
  const DefaultDockSizes sizes = defaultDockSizes(2560.0f, 1440.0f, 1.0f);

  EXPECT_FLOAT_EQ(sizes.left, defaultDockLeftWidth);
  EXPECT_FLOAT_EQ(sizes.right, defaultDockRightWidth);
  EXPECT_FLOAT_EQ(sizes.bottom, defaultDockBottomHeight);
}

TEST(DefaultDockLayoutTest, SidePanelsShrinkTogetherKeepingTheirRatio)
{
  const DefaultDockSizes sizes = defaultDockSizes(1000.0f, 1000.0f, 1.0f);

  EXPECT_NEAR(sizes.left + sizes.right, 350.0f, 0.01f);
  EXPECT_NEAR(sizes.left / sizes.right, defaultDockLeftWidth / defaultDockRightWidth, 0.001f);
}

TEST(DefaultDockLayoutTest, SidePanelsAreNeverBelowTheirFloorsUnlessTheFloorIsCapped)
{
  for (const auto& [width, height] : windowSizes)
  {
    for (const float scale : contentScales)
    {
      const EffectiveLayout layout = effectiveLayout(width, height, scale);
      const float cap = dockSideFloorFraction * width;

      EXPECT_GE(layout.left, std::min(objectsFloor * scale, cap)) << width << "x" << height << " @" << scale;
      EXPECT_GE(layout.right, std::min(inspectorFloor * scale, cap)) << width << "x" << height << " @" << scale;
    }
  }
}

TEST(DefaultDockLayoutTest, AWindowThatGrowsGetsTheTargetPixelsNotTheSmallWindowFractions)
{
  for (const float scale : contentScales)
  {
    const DefaultDockSizes small = defaultDockSizes(800.0f, 500.0f, scale);
    const float inflatedLeft = small.left / 800.0f * 2560.0f;
    const float inflatedBottom = small.bottom / 500.0f * 1440.0f;

    const DefaultDockSizes grown = defaultDockSizes(2560.0f, 1440.0f, scale);
    const float left = defaultDockFraction(DockSideRole::left, grown, 2560.0f, 1440.0f) * 2560.0f;
    const float right = defaultDockFraction(DockSideRole::right, grown, 2560.0f, 1440.0f) * 2560.0f;
    const float bottom = defaultDockFraction(DockSideRole::bottom, grown, 2560.0f, 1440.0f) * 1440.0f;

    EXPECT_LE(left, defaultDockLeftWidth * scale + 0.01f);
    EXPECT_LE(right, defaultDockRightWidth * scale + 0.01f);
    EXPECT_LE(bottom, defaultDockBottomHeight * scale + 0.01f);
    EXPECT_LE(left, inflatedLeft + 0.01f);
    EXPECT_LE(bottom, inflatedBottom + 0.01f);
  }
}

TEST(DefaultDockLayoutTest, PercentsAreRelativeToWhatEachSplitLeaves)
{
  const float width = 1920.0f;
  const float height = 1080.0f;
  const float top = 60.0f;
  const DefaultDockSizes sizes = defaultDockSizes(width, height, 1.0f);
  const DefaultDockPercents percents = defaultDockPercents(sizes, width, height, top);

  const float left = percents.left * width;
  const float right = percents.right * (width - left);
  const float topHeight = percents.top * height;
  const float bottom = percents.bottom * (height - topHeight);

  EXPECT_NEAR(left, sizes.left, 0.01f);
  EXPECT_NEAR(right, sizes.right, 0.01f);
  EXPECT_NEAR(topHeight, top, 0.01f);
  EXPECT_NEAR(bottom, sizes.bottom, 0.01f);
}

TEST(DefaultDockLayoutTest, UnusableExtentsGiveNothing)
{
  const DefaultDockSizes none = defaultDockSizes(0.0f, 720.0f, 1.0f);

  EXPECT_FLOAT_EQ(none.left, 0.0f);
  EXPECT_FLOAT_EQ(none.bottom, 0.0f);
  EXPECT_FLOAT_EQ(defaultDockPercents(none, 0.0f, 720.0f, 60.0f).left, 0.0f);
  EXPECT_FLOAT_EQ(defaultDockFraction(DockSideRole::left, none, 0.0f, 720.0f), 0.0f);
}

TEST(DefaultDockLayoutTest, RolesFollowTheAxisAndPosition)
{
  EXPECT_EQ(dockSideRole(0, 100.0f, 800.0f), DockSideRole::left);
  EXPECT_EQ(dockSideRole(0, 1500.0f, 800.0f), DockSideRole::right);
  EXPECT_EQ(dockSideRole(1, 900.0f, 500.0f), DockSideRole::bottom);
  EXPECT_EQ(dockSideRole(1, 50.0f, 500.0f), DockSideRole::other);
  EXPECT_FLOAT_EQ(defaultDockFraction(DockSideRole::other, defaultDockSizes(1920.0f, 1080.0f, 1.0f), 1920.0f,
                                      1080.0f), 0.0f);
}

TEST(DefaultDockLayoutTest, SettingAFractionReplacesTheRememberedOne)
{
  DockProportions proportions;
  proportions.remember({{1, 0, 560.0f}}, 1600.0f, 900.0f);
  proportions.setFraction(1, 0, 0.1f);

  const auto sizes = proportions.fit(2000.0f, 1000.0f);

  ASSERT_EQ(sizes.size(), 1u);
  EXPECT_FLOAT_EQ(sizes[0].size, 200.0f);
}

TEST(DefaultDockLayoutTest, SidesMovedIgnoresAClick)
{
  const std::vector<DockSideSize> before = {{1, 0, 280.0f}, {4, 0, 360.0f}};

  EXPECT_FALSE(dockSidesMoved(before, {{1, 0, 280.5f}, {4, 0, 360.0f}}));
  EXPECT_TRUE(dockSidesMoved(before, {{1, 0, 300.0f}, {4, 0, 360.0f}}));
  EXPECT_TRUE(dockSidesMoved(before, {{1, 0, 280.0f}, {4, 0, 360.0f}, {8, 1, 200.0f}}));
}
