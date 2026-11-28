#include <gtest/gtest.h>

#include "TerrainAssetUndoFixtures.h"
#include "TerrainPaletteUndo.h"
#include "Replication.h"
#include "edits/EditHistory.h"
#include "edits/RecordTerrainEdits.h"
#include "edits/TerrainRestore.h"
#include "terrain/TerrainAddition.h"
#include "terrain/TerrainStore.h"

#include <Protocol.h>
#include <nlohmann/json.hpp>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>
#include <uuid.h>

using namespace terrainAssetUndoFixtures;

namespace {
  // What EditorApp::applyTerrainPalette does, against the editor side of the fixture world.
  bool editorEdit(World& world, std::vector<terrain::TerrainMaterial> palette)
  {
    auto command = edits::commandForTerrainPalette(terrainId, world.editor.store, palette);
    if (!command)
    {
      return false;
    }

    auto description = *world.editor.store.description(terrainId);
    description.palette = std::move(palette);
    const auto asset = terrain::addTerrainAssetJson(description);

    EXPECT_TRUE(replication::applyAddAssetWithTerrain(world.editor.assets, world.editor.scenes,
                                                      world.editor.components, world.editor.store, asset, false));
    world.sendAddAsset(replication::packAddAsset(asset));
    world.history.record(*command);

    return true;
  }

  // What EditorApp::reportHistoryOutcome does with a palette payload.
  void sendHistoryPayload(World& world, const edits::HistoryOutcome& outcome, const bool applyLocally)
  {
    ASSERT_TRUE(outcome.ok());
    ASSERT_TRUE(outcome.messagePayload.has_value());

    if (applyLocally && TerrainPaletteUndo::appliesLocally(outcome, world.editor.store))
    {
      ASSERT_TRUE(replication::applyAddAssetWithTerrain(world.editor.assets, world.editor.scenes,
                                                        world.editor.components, world.editor.store,
                                                        replication::unpackAddAsset(*outcome.messagePayload), false));
    }

    world.sendAddAsset(*outcome.messagePayload);
  }

  const std::vector<terrain::TerrainMaterial>& editorPalette(const World& world)
  {
    return world.editor.store.description(terrainId)->palette;
  }

  const std::vector<terrain::TerrainMaterial>& serverPalette(const World& world)
  {
    return world.server.store.description(terrainId)->palette;
  }

  std::vector<terrain::TerrainMaterial> named(const std::initializer_list<const char*> names)
  {
    std::vector<terrain::TerrainMaterial> palette;
    for (const char* name : names)
    {
      palette.push_back({ name, {}, {} });
    }

    return palette;
  }
}

TEST(TerrainPaletteUndoApply, OnlyATerrainPaletteReplacementOfAHeldTerrainAppliesLocally)
{
  World world;
  world.create();
  ASSERT_TRUE(editorEdit(world, named({ "a" })));

  const auto undone = world.undo();
  ASSERT_TRUE(undone.ok());
  EXPECT_TRUE(TerrainPaletteUndo::appliesLocally(undone, world.editor.store));

  auto withRestore = undone;
  withRestore.terrainRestore = edits::makeTerrainRestore(*world.editor.store.description(terrainId), {}, "Hills");
  EXPECT_FALSE(TerrainPaletteUndo::appliesLocally(withRestore, world.editor.store));

  const terrain::TerrainStore emptyStore;
  EXPECT_FALSE(TerrainPaletteUndo::appliesLocally(undone, emptyStore));

  edits::HistoryOutcome otherAsset;
  otherAsset.result = edits::HistoryResult::applied;
  otherAsset.messagePayload = replication::packAddAsset({
    { "assetType", "model" }, { "uuid", uuids::to_string(terrainId) }, { "path", "models/a.obj" } });
  EXPECT_FALSE(TerrainPaletteUndo::appliesLocally(otherAsset, world.editor.store));

  edits::HistoryOutcome removal;
  removal.result = edits::HistoryResult::applied;
  removal.messagePayload = replication::packRemoveAsset(replication::buildRemoveAsset(terrainId));
  EXPECT_FALSE(TerrainPaletteUndo::appliesLocally(removal, world.editor.store));

  EXPECT_FALSE(TerrainPaletteUndo::appliesLocally(edits::HistoryOutcome{}, world.editor.store));
}

TEST(TerrainPaletteUndoApply, ARepeatedUndoAndRedoValidateAgainstThePaletteTheLocalApplyLeft)
{
  World world;
  world.create();

  ASSERT_TRUE(editorEdit(world, named({ "a" })));
  ASSERT_TRUE(editorEdit(world, named({ "a", "b" })));
  ASSERT_TRUE(editorEdit(world, named({ "a", "b", "c" })));
  ASSERT_EQ(serverPalette(world), named({ "a", "b", "c" }));

  for (const auto& expected : { named({ "a", "b" }), named({ "a" }) })
  {
    const auto undone = world.undo();
    ASSERT_TRUE(undone.ok());
    sendHistoryPayload(world, undone, true);
    EXPECT_EQ(editorPalette(world), expected);
    EXPECT_EQ(serverPalette(world), expected);
  }

  for (const auto& expected : { named({ "a", "b" }), named({ "a", "b", "c" }) })
  {
    const auto redone = world.redo();
    ASSERT_TRUE(redone.ok());
    sendHistoryPayload(world, redone, true);
    EXPECT_EQ(editorPalette(world), expected);
    EXPECT_EQ(serverPalette(world), expected);
  }
}

TEST(TerrainPaletteUndoApply, WithoutTheLocalApplyASecondUndoSeesAStalePalette)
{
  World world;
  world.create();

  ASSERT_TRUE(editorEdit(world, named({ "a" })));
  ASSERT_TRUE(editorEdit(world, named({ "a", "b" })));

  const auto first = world.undo();
  ASSERT_TRUE(first.ok());
  sendHistoryPayload(world, first, false);

  EXPECT_EQ(serverPalette(world), named({ "a" }));
  EXPECT_EQ(editorPalette(world), named({ "a", "b" }));
  EXPECT_EQ(world.undo().result, edits::HistoryResult::targetChanged);
}
