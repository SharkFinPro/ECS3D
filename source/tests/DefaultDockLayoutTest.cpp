#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <vector>

#include "DefaultDockLayout.h"
#include "DockLayoutFit.h"

namespace
{
constexpr std::array<std::array<float, 2>, 5> windowSizes = {{
  {1280.0f, 720.0f}, {1600.0f, 900.0f}, {1920.0f, 1080.0f}, {2000.0f, 1087.0f}, {2560.0f, 1440.0f}
}};
constexpr std::array<float, 4> contentScales = {1.0f, 1.25f, 1.5f, 2.0f};

// Below these logical heights the Scene Status floor and the bottom dock alone leave the scene view under 60%.
constexpr float roomyWindowLogicalHeight = 700.0f;
constexpr float tinyWindowLogicalHeight = 400.0f;

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
    std::max(sizes.left, sideFloor(objectsBaseFloor, width, scale)),
    std::max(sizes.right, sideFloor(inspectorBaseFloor, width, scale)),
    std::max(sizes.bottom, bottomFloor(assetsBaseFloor, height, scale)),
    sceneStatusBaseMinimumHeight * scale
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

      const float minimumHeight = height / scale < tinyWindowLogicalHeight     ? 0.45f
                                  : height / scale < roomyWindowLogicalHeight ? 0.5f
                                                                              : 0.6f;

      EXPECT_GE(1.0f - (layout.top + layout.bottom) / height, minimumHeight)
        << width << "x" << height << " @" << scale;
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

  EXPECT_NEAR(sizes.left + sizes.right, 340.0f, 0.01f);
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

      EXPECT_GE(layout.left + 0.01f, std::min(objectsBaseFloor * scale, cap)) << width << "x" << height << " @" << scale;
      EXPECT_GE(layout.right + 0.01f, std::min(inspectorBaseFloor * scale, cap)) << width << "x" << height << " @" << scale;
    }
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

namespace
{
constexpr std::uint32_t leftId = 1;
constexpr std::uint32_t rightId = 4;
constexpr std::uint32_t bottomId = 8;

DockSideRole roleOf(const std::uint32_t id)
{
  if (id == leftId)
  {
    return DockSideRole::left;
  }

  if (id == rightId)
  {
    return DockSideRole::right;
  }

  return id == bottomId ? DockSideRole::bottom : DockSideRole::other;
}

DockFrame frameFor(const float width, const float height, const float scale, const bool settling,
                   const std::vector<DockSideSize>& sides)
{
  DockFrame frame;
  frame.width = width;
  frame.height = height;
  frame.contentScale = scale;
  frame.settling = settling;
  frame.sides = sides;
  frame.existing = {leftId, rightId, bottomId};

  for (const auto& side : sides)
  {
    frame.roles.push_back(roleOf(side.id));
  }

  return frame;
}

std::vector<DockSideSize> defaultSides(const float width, const float height, const float scale)
{
  const DefaultDockSizes sizes = defaultDockSizes(width, height, scale);

  return {{leftId, 0, sizes.left}, {rightId, 0, sizes.right}, {bottomId, 1, sizes.bottom}};
}

float sizeOf(const DockUpdate& update, const std::uint32_t id)
{
  const auto found = std::ranges::find(update.sizes, id, &DockSideSize::id);

  return found == update.sizes.end() ? -1.0f : found->size;
}

DockFrame small(const bool settling, const std::vector<DockSideSize>& sides)
{
  return frameFor(1280.0f, 720.0f, 1.0f, settling, sides);
}

// A layout that has applied its defaults at 1280x720 and finished settling.
DockLayoutState settledDefaults()
{
  DockLayoutState state;
  state.defaultsApplied();

  for (int i = 0; i < 3; ++i)
  {
    static_cast<void>(state.update(small(true, defaultSides(1280.0f, 720.0f, 1.0f))));
  }

  static_cast<void>(state.update(small(false, defaultSides(1280.0f, 720.0f, 1.0f))));

  return state;
}

void dragLeftTo(DockLayoutState& state, const float size)
{
  const auto before = defaultSides(1280.0f, 720.0f, 1.0f);
  auto after = before;
  after[0].size = size;

  auto frame = small(false, before);
  frame.dragging = true;
  static_cast<void>(state.update(frame));

  static_cast<void>(state.update(small(false, after)));
}
}

TEST(DockLayoutStateTest, AWindowThatGrowsGetsTheDefaultPixelsNotTheSmallWindowFractions)
{
  DockLayoutState state = settledDefaults();

  const DockUpdate update =
    state.update(frameFor(2560.0f, 1440.0f, 1.0f, false, defaultSides(1280.0f, 720.0f, 1.0f)));

  EXPECT_NEAR(sizeOf(update, leftId), defaultDockLeftWidth, 0.01f);
  EXPECT_NEAR(sizeOf(update, rightId), defaultDockRightWidth, 0.01f);
  EXPECT_NEAR(sizeOf(update, bottomId), defaultDockBottomHeight, 0.01f);
}

TEST(DockLayoutStateTest, AResizeDuringTheSettleFramesYieldsTheDefaultsOfTheNewSize)
{
  DockLayoutState state;
  state.defaultsApplied();

  static_cast<void>(state.update(small(true, defaultSides(1280.0f, 720.0f, 1.0f))));
  const DockUpdate update =
    state.update(frameFor(2560.0f, 1440.0f, 1.0f, true, defaultSides(1280.0f, 720.0f, 1.0f)));

  EXPECT_NEAR(sizeOf(update, leftId), defaultDockLeftWidth, 0.01f);
  EXPECT_NEAR(sizeOf(update, bottomId), defaultDockBottomHeight, 0.01f);
}

TEST(DockLayoutStateTest, ASettlingLayoutThatIsNotAtItsDefaultsIsLeftAlone)
{
  DockLayoutState state;

  static_cast<void>(state.update(small(true, defaultSides(1280.0f, 720.0f, 1.0f))));
  const DockUpdate update =
    state.update(frameFor(2560.0f, 1440.0f, 1.0f, true, defaultSides(1280.0f, 720.0f, 1.0f)));

  EXPECT_TRUE(update.sizes.empty());
}

TEST(DockLayoutStateTest, ADragThatMovesANodeHandsTheLayoutToTheRememberedFractions)
{
  DockLayoutState state = settledDefaults();

  dragLeftTo(state, 400.0f);

  EXPECT_FALSE(state.defaultsUntouched());

  auto moved = defaultSides(1280.0f, 720.0f, 1.0f);
  moved[0].size = 400.0f;
  const DockUpdate update = state.update(frameFor(2560.0f, 1440.0f, 1.0f, false, moved));

  EXPECT_NEAR(sizeOf(update, leftId), 800.0f, 0.01f);
}

TEST(DockLayoutStateTest, AClickBelowTheToleranceKeepsTheDefaults)
{
  DockLayoutState state = settledDefaults();
  const float left = defaultDockSizes(1280.0f, 720.0f, 1.0f).left;

  dragLeftTo(state, left + dockEditTolerance * 0.5f);

  EXPECT_TRUE(state.defaultsUntouched());

  const DockUpdate update =
    state.update(frameFor(2560.0f, 1440.0f, 1.0f, false, defaultSides(1280.0f, 720.0f, 1.0f)));

  EXPECT_NEAR(sizeOf(update, leftId), defaultDockLeftWidth, 0.01f);
}

TEST(DockLayoutStateTest, ANodeTheEngineAddsAfterTheSettleFramesDoesNotClearTheDefaults)
{
  DockLayoutState state = settledDefaults();

  auto sides = defaultSides(1280.0f, 720.0f, 1.0f);
  sides.push_back({20, 0, 100.0f});
  static_cast<void>(state.update(small(false, sides)));

  EXPECT_TRUE(state.defaultsUntouched());
}

TEST(DockLayoutStateTest, ADragThatAddsANodeClearsTheDefaults)
{
  DockLayoutState state = settledDefaults();

  const auto before = defaultSides(1280.0f, 720.0f, 1.0f);
  auto after = before;
  after.push_back({20, 0, 100.0f});

  auto frame = small(false, before);
  frame.dragging = true;
  static_cast<void>(state.update(frame));
  static_cast<void>(state.update(small(false, after)));

  EXPECT_FALSE(state.defaultsUntouched());
}

TEST(DockLayoutStateTest, AContentScaleChangeAtTheSameSizeRetakesTheDefaults)
{
  DockLayoutState state = settledDefaults();

  const DockUpdate update =
    state.update(frameFor(1280.0f, 720.0f, 2.0f, false, defaultSides(1280.0f, 720.0f, 1.0f)));
  const DefaultDockSizes expected = defaultDockSizes(1280.0f, 720.0f, 2.0f);

  EXPECT_NEAR(sizeOf(update, leftId), expected.left, 0.01f);
  EXPECT_NEAR(sizeOf(update, bottomId), expected.bottom, 0.01f);
}

TEST(DockLayoutStateTest, AContentScaleChangeLeavesAHandSizedLayoutAlone)
{
  DockLayoutState state = settledDefaults();

  dragLeftTo(state, 400.0f);

  auto moved = defaultSides(1280.0f, 720.0f, 1.0f);
  moved[0].size = 400.0f;

  EXPECT_TRUE(state.update(frameFor(1280.0f, 720.0f, 2.0f, false, moved)).sizes.empty());
}

TEST(DefaultDockLayoutTest, AFreshLaunchAt2000x1087LandsOnTheHandTunedProportions)
{
  const EffectiveLayout layout = effectiveLayout(2000.0f, 1087.0f, 1.25f);

  EXPECT_NEAR(layout.left / 2000.0f, 0.157f, 0.02f);
  EXPECT_NEAR(layout.right / 2000.0f, 0.18f, 0.02f);
  EXPECT_NEAR(1.0f - (layout.left + layout.right) / 2000.0f, 0.66f, 0.025f);
  EXPECT_NEAR(layout.bottom / 1087.0f, 0.253f, 0.02f);
}
