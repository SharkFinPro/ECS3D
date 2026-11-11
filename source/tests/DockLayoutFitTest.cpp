#include <gtest/gtest.h>

#include <algorithm>
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

struct TestNode
{
  std::uint32_t ID = 0;
  int SplitAxis = -1;
  bool IsVisible = true;
  bool HasCentralNodeChild = false;
  float SizeRef[2] = {0.0f, 0.0f};
  TestNode* ParentNode = nullptr;
  TestNode* ChildNodes[2] = {nullptr, nullptr};
  bool excluded = false;

  [[nodiscard]] bool IsLeafNode() const
  {
    return ChildNodes[0] == nullptr;
  }
};

// The editor default tree: left, then (top over (center over bottom)) beside right.
struct TestTree
{
  TestNode root, left, rest, column, top, lower, center, bottom, right;

  TestTree()
  {
    root.ID = 100;
    left.ID = 1;
    rest.ID = 2;
    column.ID = 3;
    right.ID = 4;
    top.ID = 5;
    lower.ID = 6;
    center.ID = 7;
    bottom.ID = 8;

    root.SplitAxis = 0;
    rest.SplitAxis = 0;
    column.SplitAxis = 1;
    lower.SplitAxis = 1;

    root.ChildNodes[0] = &left;
    root.ChildNodes[1] = &rest;
    rest.ChildNodes[0] = &column;
    rest.ChildNodes[1] = &right;
    column.ChildNodes[0] = &top;
    column.ChildNodes[1] = &lower;
    lower.ChildNodes[0] = &center;
    lower.ChildNodes[1] = &bottom;

    for (TestNode* parent : {&root, &rest, &column, &lower})
    {
      parent->ChildNodes[0]->ParentNode = parent;
      parent->ChildNodes[1]->ParentNode = parent;
    }

    for (TestNode* node : {&root, &rest, &column, &lower, &center})
    {
      node->HasCentralNodeChild = true;
    }

    left.SizeRef[0] = 280.0f;
    right.SizeRef[0] = 360.0f;
    top.SizeRef[1] = 90.0f;
    bottom.SizeRef[1] = 240.0f;
  }

  [[nodiscard]] std::vector<DockSideSize> collect(std::vector<TestNode*>& nodes) const
  {
    std::vector<DockSideSize> sides;

    collectDockSides(const_cast<TestNode*>(&root), nodes, sides, [](const TestNode* node)
    {
      return node->excluded;
    });

    return sides;
  }
};

std::vector<std::uint32_t> idsOf(const std::vector<DockSideSize>& nodes)
{
  std::vector<std::uint32_t> ids;

  for (const auto& node : nodes)
  {
    ids.push_back(node.id);
  }

  return ids;
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

TEST(DockLayoutFitTest, FloorCapsLeaveTheCentralNodeItsBudget)
{
  EXPECT_LE(2.0f * dockSideFloorFraction, 1.0f - dockMinimumCentralFraction);
  EXPECT_LE(dockBottomFloorFraction, 1.0f - dockMinimumCentralFraction);
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

TEST(DockLayoutFitTest, GateBracketsADragAndSkipsRescaling)
{
  DockResizeGate gate;

  ASSERT_EQ(gate.update(1600.0f, 900.0f, false, false), DockLayoutStep::remember);
  EXPECT_EQ(gate.update(1000.0f, 900.0f, true, false), DockLayoutStep::beginDrag);
  EXPECT_EQ(gate.update(1000.0f, 900.0f, true, false), DockLayoutStep::none);
  EXPECT_EQ(gate.update(1000.0f, 900.0f, false, false), DockLayoutStep::endDrag);
  EXPECT_EQ(gate.update(1000.0f, 900.0f, false, false), DockLayoutStep::none);
}

TEST(DockLayoutFitTest, GateRemembersWhenTheLayoutChanged)
{
  DockResizeGate gate;

  ASSERT_EQ(gate.update(1600.0f, 900.0f, false, false), DockLayoutStep::remember);
  EXPECT_EQ(gate.update(1600.0f, 900.0f, false, true), DockLayoutStep::remember);
}

TEST(DockLayoutFitTest, TreeWalkFindsTheSideNodesOfTheDefaultLayout)
{
  TestTree tree;
  std::vector<TestNode*> nodes;

  const auto sides = tree.collect(nodes);

  ASSERT_EQ(sides.size(), 4u);
  EXPECT_FLOAT_EQ(sizeOf(sides, tree.left.ID), 280.0f);
  EXPECT_FLOAT_EQ(sizeOf(sides, tree.right.ID), 360.0f);
  EXPECT_FLOAT_EQ(sizeOf(sides, tree.top.ID), 90.0f);
  EXPECT_FLOAT_EQ(sizeOf(sides, tree.bottom.ID), 240.0f);
  EXPECT_FLOAT_EQ(sizeOf(sides, tree.center.ID), -1.0f);
}

TEST(DockLayoutFitTest, ClosedRightNodeKeepsTopAndBottom)
{
  TestTree tree;
  tree.right.IsVisible = false;
  std::vector<TestNode*> nodes;

  const auto sides = tree.collect(nodes);

  EXPECT_FLOAT_EQ(sizeOf(sides, tree.left.ID), 280.0f);
  EXPECT_FLOAT_EQ(sizeOf(sides, tree.top.ID), 90.0f);
  EXPECT_FLOAT_EQ(sizeOf(sides, tree.bottom.ID), 240.0f);
  EXPECT_FLOAT_EQ(sizeOf(sides, tree.right.ID), -1.0f);
}

TEST(DockLayoutFitTest, ClosedLeftNodeKeepsEverythingBelowIt)
{
  TestTree tree;
  tree.left.IsVisible = false;
  std::vector<TestNode*> nodes;

  const auto sides = tree.collect(nodes);

  EXPECT_FLOAT_EQ(sizeOf(sides, tree.right.ID), 360.0f);
  EXPECT_FLOAT_EQ(sizeOf(sides, tree.top.ID), 90.0f);
  EXPECT_FLOAT_EQ(sizeOf(sides, tree.bottom.ID), 240.0f);
}

TEST(DockLayoutFitTest, ContentSizedNodeIsLeftOutOfTheProportions)
{
  TestTree tree;
  tree.top.excluded = true;
  std::vector<TestNode*> nodes;

  const auto sides = tree.collect(nodes);

  EXPECT_FLOAT_EQ(sizeOf(sides, tree.top.ID), -1.0f);
  EXPECT_FLOAT_EQ(sizeOf(sides, tree.bottom.ID), 240.0f);
  EXPECT_EQ(nodes.size(), sides.size());
  EXPECT_TRUE(std::ranges::none_of(nodes, [&](const TestNode* node) { return node == &tree.top; }));
}

TEST(DockLayoutFitTest, ClickWithoutMovementKeepsTheProportionsThoughSizesAreClamped)
{
  DockProportions proportions;
  proportions.remember(defaultLayout(), 1600.0f, 900.0f);

  const std::vector<DockSideSize> clamped = {{leftId, 0, 200.0f}, {rightId, 0, 200.0f}, {bottomId, 1, 180.0f}};

  proportions.rememberChanges(clamped, clamped, idsOf(clamped), 800.0f, 600.0f);

  EXPECT_NEAR(sizeOf(proportions.fit(1600.0f, 900.0f), leftId), 280.0f, 0.01f);
  EXPECT_NEAR(sizeOf(proportions.fit(1600.0f, 900.0f), rightId), 360.0f, 0.01f);
}

TEST(DockLayoutFitTest, DragTakesOnlyTheNodesThatMoved)
{
  DockProportions proportions;
  proportions.remember(defaultLayout(), 1600.0f, 900.0f);

  const std::vector<DockSideSize> before = {{leftId, 0, 200.0f}, {rightId, 0, 200.0f}, {bottomId, 1, 180.0f}};
  const std::vector<DockSideSize> after = {{leftId, 0, 240.0f}, {rightId, 0, 200.0f}, {bottomId, 1, 180.5f}};

  proportions.rememberChanges(after, before, idsOf(after), 800.0f, 600.0f);

  const auto sizes = proportions.fit(800.0f, 600.0f);

  EXPECT_NEAR(sizeOf(sizes, leftId), 240.0f, 0.01f);
  EXPECT_NEAR(sizeOf(sizes, rightId), 180.0f, 0.01f);
  EXPECT_NEAR(sizeOf(sizes, bottomId), 240.0f / 900.0f * 600.0f, 0.01f);
}

TEST(DockLayoutFitTest, LayoutChangeKeepsKnownNodesAndTakesNewOnes)
{
  DockProportions proportions;
  proportions.remember(defaultLayout(), 1600.0f, 900.0f);

  const std::vector<DockSideSize> now = {{leftId, 0, 200.0f}, {bottomId, 1, 180.0f}, {20, 0, 160.0f}};

  EXPECT_FALSE(proportions.tracks(now));
  proportions.rememberChanges(now, now, idsOf(now), 800.0f, 600.0f);

  const auto sizes = proportions.fit(1600.0f, 900.0f);

  EXPECT_TRUE(proportions.tracks(now));
  EXPECT_NEAR(sizeOf(sizes, leftId), 280.0f, 0.01f);
  EXPECT_NEAR(sizeOf(sizes, 20), 320.0f, 0.01f);
  EXPECT_FLOAT_EQ(sizeOf(sizes, rightId), -1.0f);
}

TEST(DockLayoutFitTest, GateRemembersAndRescalesWhenTheLayoutAndSizeChangeTogether)
{
  DockResizeGate gate;

  ASSERT_EQ(gate.update(1600.0f, 900.0f, false, false), DockLayoutStep::remember);
  EXPECT_EQ(gate.update(800.0f, 600.0f, false, true), DockLayoutStep::rememberAndRescale);
  EXPECT_EQ(gate.update(800.0f, 600.0f, false, false), DockLayoutStep::none);
}

TEST(DockLayoutFitTest, ToolbarIsContentSizedOnlyAsALeafOfAVerticalStack)
{
  TestTree tree;

  EXPECT_TRUE(isContentSizedToolbarNode(&tree.top, true));
  EXPECT_FALSE(isContentSizedToolbarNode(&tree.top, false));
  EXPECT_FALSE(isContentSizedToolbarNode(&tree.right, true));
  EXPECT_FALSE(isContentSizedToolbarNode(&tree.column, true));
  EXPECT_FALSE(isContentSizedToolbarNode(&tree.root, true));
}

TEST(DockLayoutFitTest, ToolbarInASideColumnIsAnOrdinarySideNode)
{
  TestTree tree;
  std::vector<TestNode*> nodes;
  std::vector<DockSideSize> sides;

  collectDockSides(&tree.root, nodes, sides, [](const TestNode* node)
  {
    return isContentSizedToolbarNode(node, node->ID == 4);
  });

  EXPECT_FLOAT_EQ(sizeOf(sides, tree.right.ID), 360.0f);
  EXPECT_FLOAT_EQ(sizeOf(sides, tree.top.ID), 90.0f);
}

TEST(DockLayoutFitTest, TreeWalkListsHiddenNodesToo)
{
  TestTree tree;
  tree.right.IsVisible = false;
  std::vector<TestNode*> all;

  collectAllDockNodes(&tree.root, all);

  EXPECT_EQ(all.size(), 9u);
  EXPECT_TRUE(std::ranges::find(all, &tree.right) != all.end());
}

TEST(DockLayoutFitTest, ClosedNodeKeepsItsShareAndReopensAtTheFittedSize)
{
  DockProportions proportions;
  proportions.remember(defaultLayout(), 1600.0f, 900.0f);

  const std::vector<DockSideSize> visible = {{leftId, 0, 280.0f}, {bottomId, 1, 240.0f}};
  const std::vector<std::uint32_t> existing = {leftId, rightId, bottomId};

  proportions.rememberChanges(visible, visible, existing, 1600.0f, 900.0f);

  EXPECT_TRUE(proportions.tracks(visible));
  EXPECT_NEAR(sizeOf(proportions.fit(800.0f, 450.0f), rightId), 180.0f, 0.01f);

  proportions.rememberChanges(visible, visible, {leftId, bottomId}, 1600.0f, 900.0f);

  EXPECT_FLOAT_EQ(sizeOf(proportions.fit(800.0f, 450.0f), rightId), -1.0f);
}

TEST(DockLayoutFitTest, NodeWithoutASizeDoesNotCountAsUntracked)
{
  DockProportions proportions;
  proportions.remember(defaultLayout(), 1600.0f, 900.0f);

  auto nodes = defaultLayout();
  nodes.push_back({30, 0, 0.0f});

  EXPECT_TRUE(proportions.tracks(nodes));

  nodes.push_back({31, 0, 50.0f});

  EXPECT_FALSE(proportions.tracks(nodes));
}
