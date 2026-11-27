#include <gtest/gtest.h>

#include "TerrainAssetUndoFixtures.h"
#include "Replication.h"
#include "assets/AssetRegistry.h"
#include "edits/EditCommand.h"
#include "edits/RecordTerrainEdits.h"
#include "terrain/TerrainAddition.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainPalette.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainVolume.h"

#include <nlohmann/json.hpp>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <uuid.h>

using namespace terrainAssetUndoFixtures;

namespace {
  const auto albedoId = uuids::uuid::from_string("22222222-2222-4222-8222-222222222222").value();
  const auto specularId = uuids::uuid::from_string("33333333-3333-4333-8333-333333333333").value();

  std::vector<terrain::TerrainMaterial> paletteOf(const size_t count)
  {
    std::vector<terrain::TerrainMaterial> palette;

    for (size_t i = 0; i < count; ++i)
    {
      palette.push_back({ "m" + std::to_string(i), albedoId, specularId });
    }

    return palette;
  }

  std::bitset<256> usedIndices(const std::initializer_list<size_t> indices)
  {
    std::bitset<256> used;

    for (const size_t index : indices)
    {
      used.set(index);
    }

    return used;
  }

  terrain::TerrainDescription descriptionWith(const uuids::uuid& uuid, std::vector<terrain::TerrainMaterial> palette,
                                              const float voxelSize = 1.0f)
  {
    terrain::TerrainDescription description;
    description.uuid = uuid;
    description.voxelSize = voxelSize;
    description.palette = std::move(palette);

    return description;
  }

  std::shared_ptr<const terrain::TerrainChunk> denseWithMaterials(const uint8_t first, const uint8_t second)
  {
    std::vector<int16_t> distances(terrain::samplesPerChunk, 0);
    std::vector<uint8_t> materials(terrain::samplesPerChunk, first);
    materials.back() = second;

    return terrain::TerrainChunk::dense(std::move(distances), std::move(materials));
  }

  bool applyReplacement(Side& side, const nlohmann::json& asset, const bool expectUpload = false)
  {
    return replication::applyAddAssetWithTerrain(side.assets, side.scenes, side.components, side.store, asset,
                                                 expectUpload);
  }

  // A created terrain on an editor side and a server side, with helpers to set the palette on both.
  struct Pair
  {
    World world;

    Pair()
    {
      world.create();
      EXPECT_TRUE(world.serverHolds());
    }

    void setPalette(std::vector<terrain::TerrainMaterial> palette)
    {
      auto description = *world.editor.store.description(terrainId);
      description.palette = std::move(palette);
      ASSERT_TRUE(world.editor.store.applyDescription(description));
      ASSERT_TRUE(world.server.store.applyDescription(description));
    }

    [[nodiscard]] const std::vector<terrain::TerrainMaterial>& serverPalette() const
    {
      return world.server.store.description(terrainId)->palette;
    }
  };
}

TEST(TerrainPalette, ThePaletteHoldsAtMostSixteenMaterialsEverywhere)
{
  EXPECT_EQ(terrain::maxPaletteSize, 16u);

  EXPECT_TRUE(terrain::isValidDescription(descriptionWith(terrainId, paletteOf(16))));
  EXPECT_FALSE(terrain::isValidDescription(descriptionWith(terrainId, paletteOf(17))));

  EXPECT_TRUE(terrain::parseAddedTerrain(terrain::addTerrainAssetJson(descriptionWith(terrainId, paletteOf(16)))));
  EXPECT_FALSE(terrain::parseAddedTerrain(terrain::addTerrainAssetJson(descriptionWith(terrainId, paletteOf(17)))));
}

TEST(TerrainPalette, MaterialsInUseReadsDenseAndUniformChunks)
{
  terrain::TerrainVolume volume(1.0f);
  EXPECT_TRUE(terrain::materialsInUse(volume).none());

  volume.setChunk({ 0, 0, 0 }, denseWithMaterials(3, 7));
  volume.setChunk({ 1, 0, 0 }, solid(5));

  const auto used = terrain::materialsInUse(volume);
  EXPECT_EQ(used, usedIndices({ 3, 5, 7 }));
  EXPECT_FALSE(used.test(1));
  EXPECT_FALSE(used.test(0));
}

TEST(TerrainPalette, AddMaterialAppendsAndRefusesAPastTheLimitAndALongName)
{
  auto palette = paletteOf(15);

  EXPECT_FALSE(terrain::addMaterial(palette, { "sand", albedoId, {} }).has_value());
  ASSERT_EQ(palette.size(), 16u);
  EXPECT_EQ(palette.back().name, "sand");

  EXPECT_TRUE(terrain::addMaterial(palette, { "one too many", {}, {} }).has_value());
  EXPECT_EQ(palette.size(), 16u);

  auto shortPalette = paletteOf(1);
  EXPECT_TRUE(terrain::addMaterial(shortPalette, { std::string(terrain::maxMaterialNameLength + 1, 'a'), {}, {} })
                .has_value());
  EXPECT_EQ(shortPalette.size(), 1u);
  EXPECT_FALSE(terrain::addMaterial(shortPalette, { std::string(terrain::maxMaterialNameLength, 'a'), {}, {} })
                 .has_value());
}

TEST(TerrainPalette, RemoveMaterialRefusesAnyIndexAtOrBelowAPaintedOne)
{
  auto palette = paletteOf(4);

  const auto usedEntry = terrain::removeMaterial(palette, 2, usedIndices({ 2 }));
  ASSERT_TRUE(usedEntry.has_value());
  EXPECT_EQ(*usedEntry, "Material 2 is painted on the terrain");

  const auto beforeUsed = terrain::removeMaterial(palette, 1, usedIndices({ 3 }));
  ASSERT_TRUE(beforeUsed.has_value());
  EXPECT_EQ(*beforeUsed, "Material 3 is painted on the terrain");

  EXPECT_TRUE(terrain::removeMaterial(palette, 4, {}).has_value());
  EXPECT_EQ(palette, paletteOf(4));

  EXPECT_FALSE(terrain::removeMaterial(palette, 3, usedIndices({ 0, 1, 2 })).has_value());
  EXPECT_EQ(palette, paletteOf(3));

  EXPECT_FALSE(terrain::removeMaterial(palette, 0, {}).has_value());
  ASSERT_EQ(palette.size(), 2u);
  EXPECT_EQ(palette[0].name, "m1");
}

TEST(TerrainPalette, RenameMoveAndTextureEditsApplyAndRefuseBadInput)
{
  auto palette = paletteOf(3);

  EXPECT_FALSE(terrain::renameMaterial(palette, 1, "rock").has_value());
  EXPECT_EQ(palette[1].name, "rock");
  EXPECT_TRUE(terrain::renameMaterial(palette, 3, "x").has_value());
  EXPECT_TRUE(terrain::renameMaterial(palette, 1, std::string(terrain::maxMaterialNameLength + 1, 'a')).has_value());
  EXPECT_EQ(palette[1].name, "rock");

  EXPECT_FALSE(terrain::moveMaterial(palette, 0, 2).has_value());
  EXPECT_EQ(palette[0].name, "rock");
  EXPECT_EQ(palette[1].name, "m2");
  EXPECT_EQ(palette[2].name, "m0");

  EXPECT_FALSE(terrain::moveMaterial(palette, 2, 0).has_value());
  EXPECT_EQ(palette[0].name, "m0");
  EXPECT_EQ(palette[1].name, "rock");
  EXPECT_EQ(palette[2].name, "m2");

  const auto unchanged = palette;
  EXPECT_FALSE(terrain::moveMaterial(palette, 1, 1).has_value());
  EXPECT_EQ(palette, unchanged);
  EXPECT_TRUE(terrain::moveMaterial(palette, 3, 0).has_value());
  EXPECT_TRUE(terrain::moveMaterial(palette, 0, 3).has_value());
  EXPECT_EQ(palette, unchanged);

  EXPECT_FALSE(terrain::setMaterialTextures(palette, 2, specularId, albedoId).has_value());
  EXPECT_EQ(palette[2].albedoTexture, specularId);
  EXPECT_EQ(palette[2].specularTexture, albedoId);
  EXPECT_TRUE(terrain::setMaterialTextures(palette, 3, {}, {}).has_value());
}

TEST(TerrainPaletteReplacement, ABodyOverAHeldTerrainReplacesOnlyThePalette)
{
  Pair pair;
  auto& server = pair.world.server;

  const auto volume = server.store.find(terrainId);
  const auto chunksBefore = volume->snapshotChunks();
  const auto before = *server.store.description(terrainId);
  ASSERT_EQ(before.palette.size(), 0u);

  auto replaced = before;
  replaced.palette = paletteOf(2);
  replaced.chunkCount = 99;

  std::optional<terrain::TerrainDescription> created;
  EXPECT_TRUE(replication::applyAddAssetWithTerrain(server.assets, server.scenes, server.components, server.store,
                                                    terrain::addTerrainAssetJson(replaced), true, &created));

  EXPECT_FALSE(created.has_value());
  EXPECT_EQ(server.store.description(terrainId)->palette, paletteOf(2));
  EXPECT_EQ(server.store.description(terrainId)->chunkCount, before.chunkCount);
  EXPECT_FLOAT_EQ(server.store.description(terrainId)->voxelSize, before.voxelSize);
  EXPECT_EQ(server.store.find(terrainId), volume);
  EXPECT_EQ(volume->snapshotChunks(), chunksBefore);
  EXPECT_FALSE(server.store.expectedChunks(terrainId).has_value());
  EXPECT_NE(server.assets.getByUUIDOfType(terrainId, AssetType::Terrain), nullptr);
}

TEST(TerrainPaletteReplacement, AChangedVoxelSizeOrAnInvalidPaletteIsRefusedWithNothingChanged)
{
  Pair pair;
  auto& server = pair.world.server;
  pair.setPalette(paletteOf(1));

  const auto held = *server.store.description(terrainId);

  auto control = held;
  control.palette = paletteOf(3);
  EXPECT_TRUE(applyReplacement(server, terrain::addTerrainAssetJson(control)));
  EXPECT_EQ(server.store.description(terrainId)->palette.size(), 3u);
  pair.setPalette(paletteOf(1));

  auto otherVoxels = held;
  otherVoxels.voxelSize = 2.0f;
  otherVoxels.palette = paletteOf(3);
  EXPECT_FALSE(applyReplacement(server, terrain::addTerrainAssetJson(otherVoxels)));

  auto oversized = held;
  oversized.palette = paletteOf(17);
  EXPECT_FALSE(applyReplacement(server, terrain::addTerrainAssetJson(oversized)));

  EXPECT_EQ(*server.store.description(terrainId), held);
  EXPECT_EQ(server.store.find(terrainId)->chunks().size(), static_cast<size_t>(createdChunks));
}

TEST(TerrainPaletteReplacement, OtherCasesKeepTheirRefusalAndTheCreatePathStillCreates)
{
  const auto otherId = uuids::uuid::from_string("dddddddd-dddd-4ddd-8ddd-dddddddddddd").value();
  const auto components = std::make_shared<ComponentRegistry>();

  Side side;
  side.components = components;
  EXPECT_TRUE(applyReplacement(side, terrain::addTerrainAssetJson(descriptionWith(otherId, paletteOf(1))), true));
  ASSERT_NE(side.store.find(otherId), nullptr);

  // The store holds a terrain the registry does not know: refused.
  Side orphan;
  orphan.components = components;
  ASSERT_NE(orphan.store.create(descriptionWith(otherId, {})), nullptr);
  EXPECT_FALSE(applyReplacement(orphan, terrain::addTerrainAssetJson(descriptionWith(otherId, paletteOf(1)))));
  EXPECT_TRUE(orphan.store.description(otherId)->palette.empty());

  // The registry holds the uuid as another type.
  Side other;
  other.components = components;
  other.assets.registerAsset({ .uuid = otherId, .type = AssetType::Model, .path = "models/a.obj" });
  EXPECT_FALSE(applyReplacement(other, terrain::addTerrainAssetJson(descriptionWith(otherId, paletteOf(1)))));
  EXPECT_EQ(other.store.find(otherId), nullptr);
}

TEST(TerrainPaletteCommand, IsNothingForAnUnknownTerrainAnInvalidOrUnchangedPalette)
{
  Pair pair;
  const auto& store = pair.world.editor.store;

  EXPECT_FALSE(edits::commandForTerrainPalette(terrainId, store, {}).has_value());
  EXPECT_FALSE(edits::commandForTerrainPalette(terrainId, store, paletteOf(17)).has_value());

  const terrain::TerrainStore empty;
  EXPECT_FALSE(edits::commandForTerrainPalette(terrainId, empty, paletteOf(1)).has_value());

  EXPECT_TRUE(edits::commandForTerrainPalette(terrainId, store, paletteOf(1)).has_value());
}

TEST(TerrainPaletteCommand, UndoAndRedoCarryThePaletteThroughTheWire)
{
  Pair pair;
  auto& world = pair.world;
  const auto beforePalette = paletteOf(1);
  pair.setPalette(beforePalette);

  const auto command = edits::commandForTerrainPalette(terrainId, world.editor.store, paletteOf(3));
  ASSERT_TRUE(command.has_value());
  world.history.record(*command);

  auto description = *world.editor.store.description(terrainId);
  description.palette = paletteOf(3);
  const auto asset = terrain::addTerrainAssetJson(description);
  ASSERT_TRUE(applyReplacement(world.editor, asset));
  world.sendAddAsset(replication::packAddAsset(asset));
  EXPECT_EQ(pair.serverPalette(), paletteOf(3));

  const auto undone = world.undo();
  ASSERT_TRUE(undone.ok());
  ASSERT_TRUE(undone.messagePayload.has_value());
  EXPECT_FALSE(undone.terrainRestore.has_value());

  const auto undoAsset = replication::unpackAddAsset(*undone.messagePayload);
  ASSERT_TRUE(terrain::parseAddedTerrain(undoAsset).has_value());

  ASSERT_TRUE(applyReplacement(world.editor, undoAsset));
  world.sendAddAsset(*undone.messagePayload);
  EXPECT_EQ(pair.serverPalette(), beforePalette);
  EXPECT_EQ(world.editor.store.description(terrainId)->palette, beforePalette);

  const auto redone = world.redo();
  ASSERT_TRUE(redone.ok());
  ASSERT_TRUE(redone.messagePayload.has_value());
  ASSERT_TRUE(applyReplacement(world.editor, replication::unpackAddAsset(*redone.messagePayload)));
  world.sendAddAsset(*redone.messagePayload);
  EXPECT_EQ(pair.serverPalette(), paletteOf(3));
}

TEST(TerrainPaletteCommand, UndoAndRedoAreRefusedWhenTheLivePaletteDiffers)
{
  Pair pair;
  auto& world = pair.world;
  pair.setPalette(paletteOf(1));

  const auto command = edits::commandForTerrainPalette(terrainId, world.editor.store, paletteOf(3));
  ASSERT_TRUE(command.has_value());
  world.history.record(*command);

  auto description = *world.editor.store.description(terrainId);
  description.palette = paletteOf(3);
  ASSERT_TRUE(world.editor.store.applyDescription(description));

  // The live palette is the command's after palette: undo is accepted and redo is not (controls).
  EXPECT_TRUE(command->validateForUndo(*world.scene.objectManager, &world.editor.assets, nullptr,
                                       &world.editor.store).ok());
  EXPECT_FALSE(command->validateForRedo(*world.scene.objectManager, &world.editor.assets, nullptr,
                                        &world.editor.store).ok());

  description.palette = paletteOf(2);
  ASSERT_TRUE(world.editor.store.applyDescription(description));

  const auto changed = command->validateForUndo(*world.scene.objectManager, &world.editor.assets, nullptr,
                                                &world.editor.store);
  EXPECT_EQ(changed.failure, edits::ValidationFailure::targetChanged);

  const auto noStore = command->validateForUndo(*world.scene.objectManager, &world.editor.assets);
  EXPECT_EQ(noStore.failure, edits::ValidationFailure::targetMissing);

  EXPECT_EQ(world.undo().result, edits::HistoryResult::targetChanged);
}
