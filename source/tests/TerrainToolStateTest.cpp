#include <gtest/gtest.h>

#include "TerrainToolState.h"

#include <terrain/TerrainBrush.h>

#include <cstddef>
#include <optional>
#include <uuid.h>

namespace {
  uuids::uuid idFromText(const char* text)
  {
    return uuids::uuid::from_string(text).value();
  }

  const uuids::uuid terrainA = idFromText("11111111-1111-4111-8111-111111111111");
  const uuids::uuid terrainB = idFromText("22222222-2222-4222-8222-222222222222");

  TerrainModeContext contextFor(const uuids::uuid& object, const bool editable = true)
  {
    TerrainModeContext context;
    context.selectedTerrainObject = object;
    context.editable = editable;
    return context;
  }
}

TEST(TerrainToolStateTest, StartsOffWithTheAddTool)
{
  const TerrainToolState state;

  EXPECT_FALSE(state.active());
  EXPECT_FALSE(state.terrainObject().has_value());
  EXPECT_EQ(state.tool(), TerrainTool::add);
}

TEST(TerrainToolStateTest, EnterIsRefusedWithoutATerrainObject)
{
  TerrainToolState state;
  TerrainModeContext context;
  context.editable = true;

  EXPECT_FALSE(state.enter(context));
  EXPECT_FALSE(state.active());

  EXPECT_TRUE(state.enter(contextFor(terrainA)));
  EXPECT_TRUE(state.active());
}

TEST(TerrainToolStateTest, EnterIsRefusedWhenNotEditable)
{
  TerrainToolState state;

  EXPECT_FALSE(state.enter(contextFor(terrainA, false)));
  EXPECT_FALSE(state.active());
  EXPECT_FALSE(state.terrainObject().has_value());

  EXPECT_TRUE(state.enter(contextFor(terrainA, true)));
  ASSERT_TRUE(state.terrainObject().has_value());
  EXPECT_EQ(*state.terrainObject(), terrainA);
}

TEST(TerrainToolStateTest, ExitTurnsTheModeOff)
{
  TerrainToolState state;
  state.enter(contextFor(terrainA));

  state.exit();

  EXPECT_FALSE(state.active());
  EXPECT_FALSE(state.terrainObject().has_value());
}

TEST(TerrainToolStateTest, ToggleEntersThenExits)
{
  TerrainToolState state;

  EXPECT_TRUE(state.toggle(contextFor(terrainA)));
  EXPECT_TRUE(state.active());
  EXPECT_FALSE(state.toggle(contextFor(terrainA)));
  EXPECT_FALSE(state.active());
}

TEST(TerrainToolStateTest, ToggleStaysOffWhenNotEligible)
{
  TerrainToolState state;

  EXPECT_FALSE(state.toggle(contextFor(terrainA, false)));
  EXPECT_FALSE(state.active());
  EXPECT_TRUE(state.toggle(contextFor(terrainA, true)));
}

TEST(TerrainToolStateTest, ToggleExitsEvenWhenTheContextIsNoLongerEligible)
{
  TerrainToolState state;
  state.enter(contextFor(terrainA));

  EXPECT_FALSE(state.toggle(TerrainModeContext{}));
  EXPECT_FALSE(state.active());
}

TEST(TerrainToolStateTest, RefreshKeepsTheModeWhileTheContextHolds)
{
  TerrainToolState state;
  state.enter(contextFor(terrainA));

  EXPECT_FALSE(state.refresh(contextFor(terrainA)));
  EXPECT_TRUE(state.active());
}

TEST(TerrainToolStateTest, RefreshLeavesWhenTheSelectionMovesToAnotherObject)
{
  TerrainToolState state;
  state.enter(contextFor(terrainA));

  EXPECT_TRUE(state.refresh(contextFor(terrainB)));
  EXPECT_FALSE(state.active());
  EXPECT_FALSE(state.terrainObject().has_value());
}

TEST(TerrainToolStateTest, RefreshLeavesWhenTheServerStopsBeingEditable)
{
  TerrainToolState state;
  state.enter(contextFor(terrainA));

  EXPECT_TRUE(state.refresh(contextFor(terrainA, false)));
  EXPECT_FALSE(state.active());
}

TEST(TerrainToolStateTest, RefreshLeavesWhenTheObjectLosesItsTerrain)
{
  TerrainToolState state;
  state.enter(contextFor(terrainA));

  TerrainModeContext lost;
  lost.editable = true;

  EXPECT_TRUE(state.refresh(lost));
  EXPECT_FALSE(state.active());
}

TEST(TerrainToolStateTest, RefreshWhileOffDoesNothing)
{
  TerrainToolState state;

  EXPECT_FALSE(state.refresh(contextFor(terrainA)));
  EXPECT_FALSE(state.refresh(TerrainModeContext{}));
  EXPECT_FALSE(state.active());
}

TEST(TerrainToolStateTest, SetToolKeepsTheActiveState)
{
  TerrainToolState state;

  state.setTool(TerrainTool::flatten);
  EXPECT_FALSE(state.active());
  EXPECT_EQ(state.tool(), TerrainTool::flatten);

  state.enter(contextFor(terrainA));
  state.setTool(TerrainTool::smooth);

  EXPECT_TRUE(state.active());
  EXPECT_EQ(state.tool(), TerrainTool::smooth);

  state.exit();
  EXPECT_EQ(state.tool(), TerrainTool::smooth);
}

TEST(TerrainToolStateTest, BrushKindMapsEveryTool)
{
  EXPECT_EQ(terrainToolCount, 4u);
  EXPECT_EQ(brushKind(TerrainTool::add), terrain::BrushOp::Kind::add);
  EXPECT_EQ(brushKind(TerrainTool::subtract), terrain::BrushOp::Kind::subtract);
  EXPECT_EQ(brushKind(TerrainTool::smooth), terrain::BrushOp::Kind::smooth);
  EXPECT_EQ(brushKind(TerrainTool::flatten), terrain::BrushOp::Kind::flatten);
}

TEST(TerrainToolStateTest, ARunningSceneRefusesEntryAndLeavesTheModeWhenPlayStarts)
{
  TerrainToolState state;
  TerrainModeContext running = contextFor(terrainA);
  running.sceneStopped = false;

  EXPECT_FALSE(state.enter(running));
  EXPECT_FALSE(state.active());

  ASSERT_TRUE(state.enter(contextFor(terrainA)));
  EXPECT_FALSE(state.refresh(contextFor(terrainA)));
  EXPECT_TRUE(state.refresh(running));
  EXPECT_FALSE(state.active());
}
