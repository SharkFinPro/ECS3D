#include <gtest/gtest.h>

#include "TerrainRemapFlowFixtures.h"
#include "edits/EditCommand.h"
#include "edits/EditHistory.h"
#include "terrain/TerrainBrush.h"
#include "terrain/TerrainEdit.h"
#include "terrain/TerrainEditApply.h"
#include "terrain/TerrainOps.h"
#include "terrain/TerrainStore.h"

#include <glm/vec3.hpp>
#include <cstdint>
#include <vector>

using namespace remapFlowFixtures;

namespace {
  uint8_t materialAt(const Node& node, const terrain::ChunkCoord coord)
  {
    return node.volume->chunk(coord)->materials().front();
  }

  terrain::BrushOp paintBrush(const uint8_t material)
  {
    terrain::BrushOp op;
    op.kind = terrain::BrushOp::Kind::paint;
    op.center = { 80.0f, 10.0f, 80.0f };
    op.radius = 10.0f;
    op.material = material;
    return op;
  }
}

TEST(TerrainRemapUndo, ARemoveIsOneEntryWhoseUndoAndRedoMoveChunksAndPaletteTogether)
{
  RemapFlow flow;
  const Hashes original = hashesOf(*flow.server.volume);

  // Remove material 2; its ground becomes material 1 and material 3 moves down to 2.
  ASSERT_EQ(flow.remove(flow.a, connA, 2, 1), terrain::TerrainEditRefusal::none);
  flow.settle();

  EXPECT_EQ(flow.a.history.undoDepth(), 1u);
  EXPECT_EQ(flow.server.palette().size(), 3u);
  EXPECT_EQ(flow.server.palette()[2].name, "m3");
  EXPECT_EQ(materialAt(flow.server, { 0, -3, 0 }), 1);
  EXPECT_EQ(materialAt(flow.server, { 1, -3, 0 }), 2);
  EXPECT_TRUE(flow.requests.empty());
  flow.expectConverged();
  const Hashes removed = hashesOf(*flow.server.volume);
  ASSERT_NE(removed, original);

  const auto undone = flow.step(flow.a, connA, true);
  ASSERT_TRUE(undone.ok());
  flow.settle();

  EXPECT_EQ(hashesOf(*flow.server.volume), original);
  EXPECT_EQ(flow.server.palette(), namedPalette(4));
  EXPECT_FALSE(flow.a.history.canUndo());
  flow.expectConverged();

  const auto redone = flow.step(flow.a, connA, false);
  ASSERT_TRUE(redone.ok());
  flow.settle();

  EXPECT_EQ(hashesOf(*flow.server.volume), removed);
  EXPECT_EQ(flow.server.palette().size(), 3u);
  EXPECT_EQ(flow.a.history.undoDepth(), 1u);
  flow.expectConverged();
}

TEST(TerrainRemapUndo, AMoveIsOneEntryWhoseUndoAndRedoMoveChunksAndPaletteTogether)
{
  RemapFlow flow;
  const Hashes original = hashesOf(*flow.server.volume);

  ASSERT_EQ(flow.move(flow.a, connA, 1, 3), terrain::TerrainEditRefusal::none);
  flow.settle();

  EXPECT_EQ(flow.a.history.undoDepth(), 1u);
  EXPECT_EQ(flow.server.palette()[3].name, "m1");
  EXPECT_EQ(materialAt(flow.server, { 0, -3, 0 }), 1);
  EXPECT_EQ(materialAt(flow.server, { 1, -3, 0 }), 2);
  flow.expectConverged();

  ASSERT_TRUE(flow.step(flow.a, connA, true).ok());
  flow.settle();
  EXPECT_EQ(hashesOf(*flow.server.volume), original);
  EXPECT_EQ(flow.server.palette(), namedPalette(4));
  flow.expectConverged();

  ASSERT_TRUE(flow.step(flow.a, connA, false).ok());
  flow.settle();
  EXPECT_EQ(flow.server.palette()[3].name, "m1");
  EXPECT_EQ(materialAt(flow.server, { 1, -3, 0 }), 2);
  flow.expectConverged();
}

TEST(TerrainRemapUndo, UndoIsRefusedWholeWhenAnotherEditorPaintedARemappedChunk)
{
  RemapFlow flow;
  ASSERT_EQ(flow.remove(flow.a, connA, 1, 0), terrain::TerrainEditRefusal::none);
  flow.settle();

  // Control: the same removal with no paint afterward undoes.
  RemapFlow control;
  ASSERT_EQ(control.remove(control.a, connA, 1, 0), terrain::TerrainEditRefusal::none);
  control.settle();
  EXPECT_TRUE(control.step(control.a, connA, true).ok());

  const auto painted = terrain::applyBrush(*flow.server.volume, paintBrush(2));
  ASSERT_FALSE(painted.empty());
  ASSERT_EQ(flow.send(connB, terrain::packBrushEdit(terrainId, 1, paintBrush(2))), terrain::TerrainEditRefusal::none);
  flow.settle();
  flow.expectConverged();

  const Hashes before = hashesOf(*flow.server.volume);
  const auto palette = flow.server.palette();

  const auto refused = flow.step(flow.a, connA, true);
  EXPECT_EQ(refused.result, edits::HistoryResult::targetChanged);
  EXPECT_FALSE(flow.a.history.canUndo());
  flow.settle();

  EXPECT_EQ(hashesOf(*flow.server.volume), before);
  EXPECT_EQ(flow.server.palette(), palette);
  flow.expectConverged();
}

TEST(TerrainRemapUndo, ARemoveAndAMoveRacingFromTwoEditorsRefuseTheSecondAndItsViewConverges)
{
  RemapFlow flow;

  // Both edits are built on the same palette; A's reaches the server first.
  auto moveResult = TerrainPaletteEditor::move(flow.a.palette(), 0, 3);
  auto removeResult = TerrainPaletteEditor::remove(flow.b.palette(), 2, 1);
  auto movePrediction = edits::predictTerrainRemap(terrainId, flow.a.store, *moveResult.remap, moveResult.palette);
  auto removePrediction = edits::predictTerrainRemap(terrainId, flow.b.store, *removeResult.remap,
                                                     removeResult.palette);
  ASSERT_TRUE(movePrediction.has_value());
  ASSERT_TRUE(removePrediction.has_value());
  flow.a.history.record(std::move(movePrediction->command));
  flow.b.history.record(std::move(removePrediction->command));

  EXPECT_EQ(flow.send(connA, movePrediction->message), terrain::TerrainEditRefusal::none);
  const Hashes moved = hashesOf(*flow.server.volume);
  EXPECT_EQ(flow.send(connB, removePrediction->message), terrain::TerrainEditRefusal::paletteChanged);
  flow.settle();

  EXPECT_EQ(hashesOf(*flow.server.volume), moved);
  EXPECT_EQ(flow.server.palette().size(), 4u);
  EXPECT_EQ(flow.server.palette()[3].name, "m0");
  flow.expectConverged();

  // B's recorded entry no longer matches the terrain, so its undo is refused rather than applied.
  EXPECT_EQ(flow.step(flow.b, connB, true).result, edits::HistoryResult::targetChanged);
  EXPECT_TRUE(flow.step(flow.a, connA, true).ok());
  flow.settle();
  EXPECT_EQ(flow.server.palette(), namedPalette(4));
  flow.expectConverged();
}

TEST(TerrainRemapUndo, ARemapABrushOvertookOnTheSamePaletteStillConverges)
{
  RemapFlow flow;
  const auto moveResult = TerrainPaletteEditor::move(flow.a.palette(), 1, 3);
  auto prediction = edits::predictTerrainRemap(terrainId, flow.a.store, *moveResult.remap, moveResult.palette);
  ASSERT_TRUE(prediction.has_value());
  flow.a.history.record(std::move(prediction->command));

  // B adds material 3 before the server sees A's move, so A predicted from a volume the server no longer holds.
  terrain::BrushOp add = paintBrush(3);
  add.kind = terrain::BrushOp::Kind::add;
  terrain::commit(*flow.b.volume, terrain::applyBrush(*flow.b.volume, add));
  ASSERT_EQ(flow.send(connB, terrain::packBrushEdit(terrainId, 1, add)), terrain::TerrainEditRefusal::none);
  EXPECT_EQ(flow.send(connA, prediction->message), terrain::TerrainEditRefusal::none);
  flow.settle();

  EXPECT_TRUE(flow.requests.empty());
  EXPECT_GT(flow.fullEntries[connA], 0u);
  EXPECT_EQ(flow.server.palette()[3].name, "m1");
  flow.expectConverged();

  // A's entry was recorded from the stale volume, so it no longer describes the terrain and is refused whole.
  EXPECT_EQ(flow.step(flow.a, connA, true).result, edits::HistoryResult::targetChanged);
  flow.expectConverged();
}
