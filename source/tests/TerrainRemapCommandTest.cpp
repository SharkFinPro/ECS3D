#include <gtest/gtest.h>

#include "TerrainMaterialPrompts.h"
#include "TerrainPaletteEditor.h"
#include "TestScene.h"
#include "edits/EditCommand.h"
#include "edits/TerrainSetChunks.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainEdit.h"
#include "terrain/TerrainMaterialRemap.h"
#include "terrain/TerrainPalette.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainVolume.h"

#include <Protocol.h>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <uuid.h>

namespace {
  const auto terrainId = uuids::uuid::from_string("99999999-9999-4999-8999-999999999999").value();
  const auto otherTerrain = uuids::uuid::from_string("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa").value();

  std::vector<terrain::TerrainMaterial> paletteOf(const size_t count, const std::string& prefix = "m")
  {
    std::vector<terrain::TerrainMaterial> palette;

    for (size_t i = 0; i < count; ++i)
    {
      palette.push_back({ .name = prefix + std::to_string(i) });
    }

    return palette;
  }

  terrain::MaterialRemap swapRemap()
  {
    auto remap = terrain::identityRemap();
    remap[0] = 1;
    remap[1] = 0;
    return remap;
  }

  // A terrain holding the after state of a recorded swap of materials 0 and 1 over one chunk.
  struct Recorded
  {
    terrain::TerrainStore store;
    std::shared_ptr<terrain::TerrainVolume> volume;
    std::shared_ptr<const terrain::TerrainChunk> before = terrain::TerrainChunk::uniform(-10, 0);
    std::shared_ptr<const terrain::TerrainChunk> after = terrain::TerrainChunk::uniform(-10, 1);
    edits::EditCommand command = edits::EditCommand::terrainPaletteRemap(
      terrainId, { { { 0, 0, 0 }, before, after } }, { paletteOf(2), paletteOf(2, "n"), swapRemap() });

    Recorded()
    {
      volume = store.create({ .uuid = terrainId, .palette = paletteOf(2, "n") });
      volume->setChunk({ 0, 0, 0 }, after);
    }

    void setPalette(std::vector<terrain::TerrainMaterial> palette)
    {
      auto description = *store.description(terrainId);
      description.palette = std::move(palette);
      ASSERT_TRUE(store.applyDescription(description));
    }

    [[nodiscard]] edits::Validation undo(const fixtures::Scene& scene) const
    {
      return command.validateForUndo(*scene.objectManager, nullptr, nullptr, &store);
    }

    [[nodiscard]] edits::Validation redo(const fixtures::Scene& scene) const
    {
      return command.validateForRedo(*scene.objectManager, nullptr, nullptr, &store);
    }
  };
}

TEST(TerrainRemapCommand, APaletteRemapMayRecordNoChunkWhileAPlainStrokeMayNot)
{
  const auto command = edits::EditCommand::terrainPaletteRemap(terrainId, {}, { paletteOf(2), paletteOf(2, "n"),
                                                                              swapRemap() });
  EXPECT_EQ(command.kind(), edits::CommandKind::terrainStroke);
  EXPECT_EQ(command.pinnedBytes(), 0u);
  EXPECT_THROW(static_cast<void>(edits::EditCommand::terrainStroke(terrainId, {})), std::invalid_argument);
}

TEST(TerrainRemapCommand, ItsLabelNamesTheMaterialsAndItPinsItsChunks)
{
  fixtures::Scene scene;
  const Recorded recorded;
  EXPECT_EQ(recorded.command.describeForMenu(*scene.objectManager, nullptr), "Change Terrain Materials");
  EXPECT_EQ(recorded.command.pinnedBytes(), recorded.before->byteSize() + recorded.after->byteSize());

  const auto stroke =
    edits::EditCommand::terrainStroke(terrainId, { { { 0, 0, 0 }, recorded.before, recorded.after } });
  EXPECT_EQ(stroke.describeForMenu(*scene.objectManager, nullptr), "Sculpt Terrain");
}

TEST(TerrainRemapCommand, UndoNeedsTheAfterChunksAndTheAfterPalette)
{
  fixtures::Scene scene;
  Recorded recorded;
  EXPECT_TRUE(recorded.undo(scene).ok());
  EXPECT_FALSE(recorded.redo(scene).ok());

  recorded.setPalette(paletteOf(2));
  EXPECT_EQ(recorded.undo(scene).failure, edits::ValidationFailure::targetChanged);

  recorded.setPalette(paletteOf(2, "n"));
  recorded.volume->setChunk({ 0, 0, 0 }, recorded.before);
  EXPECT_EQ(recorded.undo(scene).failure, edits::ValidationFailure::targetChanged);

  // The before state on both halves is what redo needs.
  recorded.setPalette(paletteOf(2));
  EXPECT_TRUE(recorded.redo(scene).ok());
}

TEST(TerrainRemapCommand, UndoCarriesTheBeforePaletteAndRedoTheRemapForIt)
{
  const Recorded recorded;

  const auto undo = recorded.command.buildUndoTerrainChunks();
  ASSERT_TRUE(undo.palette.has_value());
  EXPECT_EQ(undo.palette->palette, paletteOf(2));
  EXPECT_FALSE(undo.palette->remap.has_value());
  ASSERT_EQ(undo.chunks.size(), 1u);
  EXPECT_EQ(undo.chunks[0].chunk, recorded.before);

  const auto redo = recorded.command.buildRedoTerrainChunks();
  ASSERT_TRUE(redo.palette.has_value());
  EXPECT_EQ(redo.palette->palette, paletteOf(2, "n"));
  EXPECT_EQ(redo.palette->remap, swapRemap());
  EXPECT_EQ(redo.palette->fromPaletteHash, terrain::paletteHash(paletteOf(2)));

  const auto stroke =
    edits::EditCommand::terrainStroke(terrainId, { { { 0, 0, 0 }, recorded.before, recorded.after } });
  EXPECT_FALSE(stroke.buildUndoTerrainChunks().palette.has_value());
}

TEST(TerrainRemapCommand, AnUndoStepPacksTheChunksThenThePaletteAndARedoStepPacksOneRemap)
{
  Recorded recorded;

  const auto undoMessages = edits::packTerrainStep(recorded.command.buildUndoTerrainChunks(), recorded.store);
  ASSERT_EQ(undoMessages.size(), 2u);
  EXPECT_EQ(undoMessages[0].getType(), net::MessageType::terrainEdit);
  EXPECT_EQ(undoMessages[1].getType(), net::MessageType::addAsset);

  const auto redoMessages = edits::packTerrainStep(recorded.command.buildRedoTerrainChunks(), recorded.store);
  ASSERT_EQ(redoMessages.size(), 1u);
  const auto remap = terrain::unpackTerrainEdit(redoMessages[0]);
  ASSERT_TRUE(remap.has_value());
  EXPECT_EQ(remap->kind, terrain::TerrainEditKind::remapMaterials);
  EXPECT_EQ(remap->palette, paletteOf(2, "n"));

  auto elsewhere = recorded.command.buildUndoTerrainChunks();
  elsewhere.terrain = otherTerrain;
  EXPECT_TRUE(edits::packTerrainStep(elsewhere, recorded.store).empty());
}

TEST(TerrainRemapCommand, PredictingAStepAppliesTheChunksOrTheRemapAndThePalette)
{
  Recorded recorded;

  ASSERT_TRUE(edits::predictTerrainStep(recorded.store, recorded.command.buildUndoTerrainChunks()));
  EXPECT_EQ(recorded.volume->chunk({ 0, 0, 0 }), recorded.before);
  EXPECT_EQ(recorded.store.description(terrainId)->palette, paletteOf(2));

  ASSERT_TRUE(edits::predictTerrainStep(recorded.store, recorded.command.buildRedoTerrainChunks()));
  EXPECT_EQ(recorded.volume->chunk({ 0, 0, 0 })->hash(), recorded.after->hash());
  EXPECT_EQ(recorded.store.description(terrainId)->palette, paletteOf(2, "n"));

  auto invalid = recorded.command.buildUndoTerrainChunks();
  invalid.palette->palette[0].color.r = 2.0f;
  EXPECT_FALSE(edits::predictTerrainStep(recorded.store, invalid));
  EXPECT_EQ(recorded.volume->chunk({ 0, 0, 0 })->hash(), recorded.after->hash());
  EXPECT_EQ(recorded.store.description(terrainId)->palette, paletteOf(2, "n"));
}

TEST(TerrainMaterialGate, ARemovalOrMoveWaitsForTheWholeTerrainAnIdleBrushAndAStoppedScene)
{
  const TerrainPaletteEditor::Context ready{ true, true, true, true, false };
  EXPECT_FALSE(TerrainPaletteEditor::remapRefusal(ready).has_value());

  auto incomplete = ready;
  incomplete.terrainComplete = false;
  EXPECT_TRUE(TerrainPaletteEditor::remapRefusal(incomplete).has_value());
  EXPECT_FALSE(TerrainPaletteEditor::editRefusal(incomplete).has_value());

  auto busy = ready;
  busy.strokeBusy = true;
  EXPECT_TRUE(TerrainPaletteEditor::remapRefusal(busy).has_value());

  auto playing = ready;
  playing.sceneStopped = false;
  EXPECT_EQ(TerrainPaletteEditor::remapRefusal(playing), TerrainPaletteEditor::editRefusal(playing));
  EXPECT_TRUE(TerrainPaletteEditor::remapRefusal(playing).has_value());

  auto readOnly = ready;
  readOnly.editable = false;
  EXPECT_TRUE(TerrainPaletteEditor::remapRefusal(readOnly).has_value());
}

TEST(TerrainMaterialGate, ARemovePromptTheBlockStopsDrawingCloses)
{
  MaterialRemovePrompt prompt;
  prompt.open(terrainId, paletteOf(3), 1);
  prompt.markDrawn();
  prompt.endFrame();
  EXPECT_TRUE(prompt.isOpen());

  prompt.endFrame();
  EXPECT_FALSE(prompt.isOpen());
}

TEST(TerrainMaterialGate, TheSelectionFollowsOnlyAMoveOrRemoveOfTheShownTerrain)
{
  const TextureModeRows::Change move{ TextureModeRows::Change::Kind::move, 1, 3 };
  EXPECT_EQ(selectionToFollow(move, terrainId, terrainId, 1, 4), std::optional<size_t>(3));
  EXPECT_FALSE(selectionToFollow(move, terrainId, otherTerrain, 1, 4).has_value());
  EXPECT_FALSE(selectionToFollow({}, terrainId, terrainId, 1, 4).has_value());

  const TextureModeRows::Change remove{ TextureModeRows::Change::Kind::remove, 3, 3 };
  EXPECT_EQ(selectionToFollow(remove, terrainId, terrainId, 3, 3), std::optional<size_t>(2));
}
