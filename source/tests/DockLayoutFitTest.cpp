#include <gtest/gtest.h>

#include <vector>

#include "DockLayoutFit.h"

namespace
{
constexpr std::uint32_t leftId = 1;
constexpr std::uint32_t rightId = 4;
constexpr std::uint32_t bottomId = 8;

std::vector<DockSideSize> defaultLayout()
{
  return {{leftId, 0, 280.0f}, {rightId, 0, 360.0f}, {bottomId, 1, 240.0f}};
}

float sizeOf(const std::vector<DockSideSize>& sizes, const std::uint32_t id)
{
  for (const auto& size : sizes)
  {
    if (size.id == id)
    {
      return size.size;
    }
  }

  return -1.0f;
}
}

TEST(DockLayoutFitTest, ShrinkingKeepsEachNodesShareOfTheWindow)
{
  DockProportions proportions;
  proportions.remember(defaultLayout(), 1600.0f, 900.0f);

  const auto sizes = proportions.fit(800.0f, 450.0f);

  EXPECT_NEAR(sizeOf(sizes, leftId), 140.0f, 0.01f);
  EXPECT_NEAR(sizeOf(sizes, rightId), 180.0f, 0.01f);
  EXPECT_NEAR(sizeOf(sizes, bottomId), 120.0f, 0.01f);
}

TEST(DockLayoutFitTest, GrowingBackRestoresTheOriginalSizes)
{
  DockProportions proportions;
  proportions.remember(defaultLayout(), 1600.0f, 900.0f);

  static_cast<void>(proportions.fit(500.0f, 300.0f));
  const auto sizes = proportions.fit(1600.0f, 900.0f);

  EXPECT_NEAR(sizeOf(sizes, leftId), 280.0f, 0.01f);
  EXPECT_NEAR(sizeOf(sizes, rightId), 360.0f, 0.01f);
  EXPECT_NEAR(sizeOf(sizes, bottomId), 240.0f, 0.01f);
}

TEST(DockLayoutFitTest, CentralNodeKeepsItsMinimumShare)
{
  DockProportions proportions;
  proportions.remember({{leftId, 0, 700.0f}, {rightId, 0, 700.0f}}, 1600.0f, 900.0f);

  const auto sizes = proportions.fit(1600.0f, 900.0f);
  const float central = 1600.0f - sizeOf(sizes, leftId) - sizeOf(sizes, rightId);

  EXPECT_GE(central, 1600.0f * dockMinimumCentralFraction - 0.01f);
  EXPECT_NEAR(sizeOf(sizes, leftId), sizeOf(sizes, rightId), 0.01f);
  EXPECT_GT(sizeOf(sizes, leftId), 0.0f);
}

TEST(DockLayoutFitTest, ProportionsWithinTheBudgetAreNotShrunk)
{
  DockProportions proportions;
  proportions.remember({{leftId, 0, 400.0f}, {rightId, 0, 400.0f}}, 1600.0f, 900.0f);

  const auto sizes = proportions.fit(1600.0f, 900.0f);

  EXPECT_NEAR(sizeOf(sizes, leftId), 400.0f, 0.01f);
  EXPECT_NEAR(sizeOf(sizes, rightId), 400.0f, 0.01f);
}

TEST(DockLayoutFitTest, HandResizedLayoutKeepsItsOwnProportions)
{
  DockProportions proportions;
  proportions.remember({{leftId, 0, 480.0f}, {rightId, 0, 160.0f}}, 1600.0f, 900.0f);

  const auto sizes = proportions.fit(800.0f, 900.0f);

  EXPECT_NEAR(sizeOf(sizes, leftId), 240.0f, 0.01f);
  EXPECT_NEAR(sizeOf(sizes, rightId), 80.0f, 0.01f);
  EXPECT_NEAR(sizeOf(sizes, leftId) / sizeOf(sizes, rightId), 3.0f, 0.001f);
}

TEST(DockLayoutFitTest, UnusableExtentsDoNothing)
{
  DockProportions proportions;
  proportions.remember(defaultLayout(), 1600.0f, 900.0f);

  EXPECT_TRUE(proportions.fit(0.0f, 0.0f).empty());
  EXPECT_TRUE(proportions.fit(800.0f, 0.0f).empty());
  EXPECT_FALSE(proportions.fit(800.0f, 450.0f).empty());

  proportions.remember({{leftId, 0, 10.0f}}, 0.0f, 0.0f);
  EXPECT_NEAR(sizeOf(proportions.fit(1600.0f, 900.0f), leftId), 280.0f, 0.01f);
  EXPECT_TRUE(proportions.tracks(defaultLayout()));
}

TEST(DockLayoutFitTest, TracksOnlyTheRememberedNodes)
{
  DockProportions proportions;
  proportions.remember(defaultLayout(), 1600.0f, 900.0f);

  auto changed = defaultLayout();
  changed.push_back({20, 0, 100.0f});

  EXPECT_TRUE(proportions.tracks(defaultLayout()));
  EXPECT_FALSE(proportions.tracks(changed));
  EXPECT_FALSE(DockProportions().tracks(defaultLayout()));
}

TEST(DockLayoutFitTest, FloorsShrinkWithTheWindowButNeverGrow)
{
  EXPECT_FLOAT_EQ(dockFloorForExtent(260.0f, 800.0f, 0.25f), 200.0f);
  EXPECT_FLOAT_EQ(dockFloorForExtent(260.0f, 1600.0f, 0.25f), 260.0f);
  EXPECT_FLOAT_EQ(dockFloorForExtent(260.0f, 0.0f, 0.25f), 260.0f);
}

TEST(DockLayoutFitTest, TwoSideFloorsLeaveTheCentralMinimum)
{
  constexpr float sideFloorFraction = 0.25f;

  EXPECT_LE(2.0f * sideFloorFraction, 1.0f - dockMinimumCentralFraction);
}

TEST(DockLayoutFitTest, GateRemembersFirstThenRescalesOnlyOnAChange)
{
  DockResizeGate gate;

  EXPECT_EQ(gate.update(1600.0f, 900.0f, false, false), DockLayoutStep::remember);
  EXPECT_EQ(gate.update(1600.0f, 900.0f, false, false), DockLayoutStep::none);
  EXPECT_EQ(gate.update(800.0f, 600.0f, false, false), DockLayoutStep::rescale);
  EXPECT_EQ(gate.update(800.0f, 600.0f, false, false), DockLayoutStep::none);
  EXPECT_EQ(gate.update(1600.0f, 900.0f, false, false), DockLayoutStep::rescale);
}

TEST(DockLayoutFitTest, GateIgnoresAMinimizedWindowAndResumesUnchanged)
{
  DockResizeGate gate;

  EXPECT_EQ(gate.update(1600.0f, 900.0f, false, false), DockLayoutStep::remember);
  EXPECT_EQ(gate.update(0.0f, 0.0f, false, false), DockLayoutStep::none);
  EXPECT_EQ(gate.update(0.0f, 0.0f, false, true), DockLayoutStep::none);
  EXPECT_EQ(gate.update(1600.0f, 900.0f, false, false), DockLayoutStep::none);
}

TEST(DockLayoutFitTest, MinimizeAndRestoreGivesBackTheSameLayout)
{
  DockProportions proportions;
  DockResizeGate gate;

  ASSERT_EQ(gate.update(1600.0f, 900.0f, false, false), DockLayoutStep::remember);
  proportions.remember(defaultLayout(), 1600.0f, 900.0f);

  ASSERT_EQ(gate.update(800.0f, 600.0f, false, false), DockLayoutStep::rescale);
  EXPECT_EQ(gate.update(0.0f, 0.0f, false, false), DockLayoutStep::none);
  EXPECT_TRUE(proportions.fit(0.0f, 0.0f).empty());
  EXPECT_EQ(gate.update(800.0f, 600.0f, false, false), DockLayoutStep::none);
  ASSERT_EQ(gate.update(1600.0f, 900.0f, false, false), DockLayoutStep::rescale);

  EXPECT_NEAR(sizeOf(proportions.fit(1600.0f, 900.0f), leftId), 280.0f, 0.01f);
}

TEST(DockLayoutFitTest, GateWaitsOutADragThenRemembers)
{
  DockResizeGate gate;

  ASSERT_EQ(gate.update(1600.0f, 900.0f, false, false), DockLayoutStep::remember);
  EXPECT_EQ(gate.update(1000.0f, 900.0f, true, false), DockLayoutStep::none);
  EXPECT_EQ(gate.update(1000.0f, 900.0f, true, false), DockLayoutStep::none);
  EXPECT_EQ(gate.update(1000.0f, 900.0f, false, false), DockLayoutStep::remember);
  EXPECT_EQ(gate.update(1000.0f, 900.0f, false, false), DockLayoutStep::none);
}

TEST(DockLayoutFitTest, GateRemembersWhenTheLayoutChanged)
{
  DockResizeGate gate;

  ASSERT_EQ(gate.update(1600.0f, 900.0f, false, false), DockLayoutStep::remember);
  EXPECT_EQ(gate.update(1600.0f, 900.0f, false, true), DockLayoutStep::remember);
}
