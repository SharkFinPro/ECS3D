#include <gtest/gtest.h>

#include "TerrainToolState.h"

#include <terrain/TerrainBrush.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>
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
  EXPECT_EQ(terrainToolCount, 5u);
  EXPECT_EQ(brushKind(TerrainTool::add), terrain::BrushOp::Kind::add);
  EXPECT_EQ(brushKind(TerrainTool::subtract), terrain::BrushOp::Kind::subtract);
  EXPECT_EQ(brushKind(TerrainTool::smooth), terrain::BrushOp::Kind::smooth);
  EXPECT_EQ(brushKind(TerrainTool::flatten), terrain::BrushOp::Kind::flatten);
  EXPECT_EQ(brushKind(TerrainTool::paint), terrain::BrushOp::Kind::paint);
}

TEST(TerrainToolStateTest, ThePaintToolCanBeSetAndRead)
{
  TerrainToolState state;
  state.setTool(TerrainTool::paint);

  EXPECT_EQ(state.tool(), TerrainTool::paint);
}

TEST(TerrainToolStateTest, TheActiveMaterialDefaultsToZeroAndSurvivesExitAndEnter)
{
  TerrainToolState state;
  EXPECT_EQ(state.activeMaterial(), 0);

  ASSERT_TRUE(state.enter(contextFor(terrainA)));
  state.setActiveMaterial(7);
  EXPECT_EQ(state.activeMaterial(), 7);

  state.exit();
  EXPECT_EQ(state.activeMaterial(), 7);

  ASSERT_TRUE(state.enter(contextFor(terrainB)));
  EXPECT_EQ(state.activeMaterial(), 7);
}

TEST(TerrainToolStateTest, ClampMaterialKeepsAnIndexInsideThePaletteAndElseTakesTheLastEntry)
{
  EXPECT_EQ(clampMaterial(0, 4), 0);
  EXPECT_EQ(clampMaterial(3, 4), 3);
  EXPECT_EQ(clampMaterial(4, 4), 3);
  EXPECT_EQ(clampMaterial(200, 4), 3);
  EXPECT_EQ(clampMaterial(5, 0), 0);
  EXPECT_EQ(clampMaterial(0, 0), 0);
  EXPECT_EQ(clampMaterial(2, 1), 0);
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

namespace {
  std::vector<terrain::TerrainMaterial> paletteOf(const std::vector<std::string>& names)
  {
    std::vector<terrain::TerrainMaterial> palette;

    for (const std::string& name : names)
    {
      palette.push_back({ name, {}, {} });
    }

    return palette;
  }
}

TEST(TerrainToolStateTest, AnEmptyPaletteOffersTheEightDefaultColors)
{
  const std::vector<terrain::TerrainMaterial> none;

  EXPECT_EQ(selectableMaterials(none), 8u);
  EXPECT_EQ(materialLabel(none, 0), "Default 0");
  EXPECT_EQ(materialLabel(none, 7), "Default 7");
}

TEST(TerrainToolStateTest, APaletteOffersItsEntriesLabeledByIndexAndName)
{
  const auto palette = paletteOf({ "Grass", "Dirt", "Sand", "Rock" });

  EXPECT_EQ(selectableMaterials(palette), 4u);
  EXPECT_EQ(materialLabel(palette, 0), "0: Grass");
  EXPECT_EQ(materialLabel(palette, 3), "3: Rock");
}

TEST(TerrainToolStateTest, AnUnnamedEntryGetsAFallbackName)
{
  const auto palette = paletteOf({ "Grass", "", "Sand", "" });

  EXPECT_EQ(materialLabel(palette, 3), "3: Material 3");
  EXPECT_EQ(materialLabel(palette, 0), "0: Grass");
}

TEST(TerrainToolStateTest, AnEmptyPaletteSwatchIsTheRenderersDefaultColorWrappedByIndex)
{
  const std::vector<terrain::TerrainMaterial> none;

  for (uint8_t index = 0; index < 8; ++index)
  {
    EXPECT_EQ(materialSwatchColor(none, index), vke::s_defaultVolumeColors[index]);
  }

  EXPECT_EQ(materialSwatchColor(none, 9), vke::s_defaultVolumeColors[1]);
  EXPECT_NE(materialSwatchColor(none, 0), materialSwatchColor(none, 1));
}

TEST(TerrainToolStateTest, APaletteEntrySwatchIsNeutral)
{
  const std::vector<terrain::TerrainMaterial> palette(3);

  EXPECT_EQ(materialSwatchColor(palette, 2), glm::vec3(0.5f));
}

TEST(TerrainToolStateTest, ButtonsStayOnOneRowWhenTheyFit)
{
  const std::vector<float> widths = { 40.0f, 60.0f, 50.0f };

  EXPECT_EQ(buttonRowBreaks(widths, 10.0f, 180.0f), (std::vector<bool>{ false, false, false }));
  EXPECT_EQ(buttonRowBreaks(widths, 10.0f, 170.0f), (std::vector<bool>{ false, false, false }));
}

TEST(TerrainToolStateTest, AButtonThatDoesNotFitStartsANewRow)
{
  const std::vector<float> widths = { 40.0f, 60.0f, 50.0f, 30.0f };

  EXPECT_EQ(buttonRowBreaks(widths, 10.0f, 169.0f), (std::vector<bool>{ false, false, true, false }));
  EXPECT_EQ(buttonRowBreaks(widths, 10.0f, 110.0f), (std::vector<bool>{ false, false, true, false }));
  EXPECT_EQ(buttonRowBreaks(widths, 10.0f, 60.0f), (std::vector<bool>{ false, true, true, true }));
}

TEST(TerrainToolStateTest, AButtonWiderThanTheRowStillGetsItsOwnRowAndTheFirstNeverBreaks)
{
  const std::vector<float> widths = { 90.0f, 20.0f };

  EXPECT_EQ(buttonRowBreaks(widths, 5.0f, 50.0f), (std::vector<bool>{ false, true }));
  EXPECT_TRUE(buttonRowBreaks({}, 5.0f, 50.0f).empty());
}
