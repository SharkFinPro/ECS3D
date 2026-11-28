#include <gtest/gtest.h>

#include "edits/RecordTerrainEdits.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainEdit.h"
#include "terrain/TerrainMaterialRemap.h"
#include "terrain/TerrainPalette.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <Protocol.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace {
  using terrain::ChunkCoord;
  using terrain::MaterialRemap;

  const auto terrainId = uuids::uuid::from_string("33333333-3333-3333-3333-333333333333").value();

  // Distances and materials that both vary, with air samples (positive distance) carrying material 2.
  std::shared_ptr<const terrain::TerrainChunk> mixedChunk()
  {
    std::vector<int16_t> distances(terrain::samplesPerChunk);
    std::vector<uint8_t> materials(terrain::samplesPerChunk);

    for (size_t i = 0; i < distances.size(); ++i)
    {
      distances[i] = static_cast<int16_t>(static_cast<int>(i % 200) - 100);
      materials[i] = distances[i] > 0 ? 2 : static_cast<uint8_t>(i % 2);
    }

    return terrain::TerrainChunk::dense(std::move(distances), std::move(materials));
  }

  MaterialRemap mapping(const uint8_t from, const uint8_t to)
  {
    MaterialRemap remap = terrain::identityRemap();
    remap[from] = to;
    return remap;
  }

  // The palette's names after an edit, so a remap can be checked against what moveMaterial / removeMaterial do.
  std::vector<std::string> names(const size_t size)
  {
    std::vector<std::string> result;
    for (size_t i = 0; i < size; ++i)
    {
      result.push_back("m" + std::to_string(i));
    }

    return result;
  }

  std::vector<terrain::TerrainMaterial> palette(const size_t size)
  {
    std::vector<terrain::TerrainMaterial> result;
    for (const auto& name : names(size))
    {
      result.push_back({ .name = name });
    }

    return result;
  }
}

TEST(TerrainMaterialRemap, IdentityIsValidAndIdentity)
{
  EXPECT_TRUE(terrain::isIdentity(terrain::identityRemap()));
  EXPECT_TRUE(terrain::isValidRemap(terrain::identityRemap()));
  EXPECT_FALSE(terrain::isIdentity(mapping(1, 2)));
  EXPECT_TRUE(terrain::isValidRemap(mapping(1, 2)));
}

TEST(TerrainMaterialRemap, TheReservedIndexMustMapToItselfAndNothingElseToIt)
{
  EXPECT_TRUE(terrain::isValidRemap(mapping(3, 254)));
  EXPECT_FALSE(terrain::isValidRemap(mapping(terrain::reservedMaterial, 0)));
  EXPECT_FALSE(terrain::isValidRemap(mapping(3, terrain::reservedMaterial)));
}

TEST(TerrainMaterialRemap, RemovalSendsTheRemovedToTheReplacementAndShiftsLaterIndicesDown)
{
  // Remove 1 of 4, replaced by 3 (which becomes 2 after the removal).
  const auto remap = terrain::remapForRemoval(4, 1, 3);
  ASSERT_TRUE(remap.has_value());
  EXPECT_EQ((*remap)[0], 0);
  EXPECT_EQ((*remap)[1], 2);
  EXPECT_EQ((*remap)[2], 1);
  EXPECT_EQ((*remap)[3], 2);
  EXPECT_EQ((*remap)[254], 253);
  EXPECT_EQ((*remap)[terrain::reservedMaterial], terrain::reservedMaterial);
  EXPECT_TRUE(terrain::isValidRemap(*remap));

  // Each sample still names the entry it showed (or the replacement) once the palette loses entry 1.
  auto removed = palette(4);
  ASSERT_FALSE(terrain::removeMaterial(removed, 1, {}).has_value());
  const auto before = names(4);
  for (const size_t index : { 0u, 2u, 3u })
  {
    EXPECT_EQ(removed[(*remap)[index]].name, before[index]);
  }
  EXPECT_EQ(removed[(*remap)[1]].name, "m3");

  const auto earlier = terrain::remapForRemoval(4, 2, 0);
  ASSERT_TRUE(earlier.has_value());
  EXPECT_EQ((*earlier)[2], 0);
  EXPECT_EQ((*earlier)[1], 1);
  EXPECT_EQ((*earlier)[3], 2);
}

TEST(TerrainMaterialRemap, RemovalRefusesOutOfRangeAndASelfReplacement)
{
  EXPECT_TRUE(terrain::remapForRemoval(4, 3, 0).has_value());
  EXPECT_FALSE(terrain::remapForRemoval(4, 4, 0).has_value());
  EXPECT_FALSE(terrain::remapForRemoval(4, 0, 4).has_value());
  EXPECT_FALSE(terrain::remapForRemoval(4, 2, 2).has_value());
  EXPECT_FALSE(terrain::remapForRemoval(terrain::maxPaletteSize + 1, 0, 1).has_value());
  EXPECT_FALSE(terrain::remapForRemoval(0, 0, 0).has_value());
}

TEST(TerrainMaterialRemap, MoveLetsSamplesFollowTheirMaterialBothWays)
{
  const std::vector<std::pair<size_t, size_t>> moves{ { 1, 4 }, { 4, 1 }, { 0, 5 }, { 5, 0 } };
  for (const auto& [from, to] : moves)
  {
    const auto remap = terrain::remapForMove(6, from, to);
    ASSERT_TRUE(remap.has_value());
    EXPECT_TRUE(terrain::isValidRemap(*remap));

    auto moved = palette(6);
    ASSERT_FALSE(terrain::moveMaterial(moved, from, to).has_value());
    const auto before = names(6);
    for (size_t index = 0; index < 6; ++index)
    {
      EXPECT_EQ(moved[(*remap)[index]].name, before[index]) << from << "->" << to << " at " << index;
    }

    EXPECT_EQ((*remap)[6], 6);
    EXPECT_EQ((*remap)[terrain::reservedMaterial], terrain::reservedMaterial);
  }
}

TEST(TerrainMaterialRemap, MoveInPlaceIsTheIdentityAndOutOfRangeIsRefused)
{
  const auto same = terrain::remapForMove(3, 2, 2);
  ASSERT_TRUE(same.has_value());
  EXPECT_TRUE(terrain::isIdentity(*same));
  EXPECT_FALSE(terrain::remapForMove(3, 3, 0).has_value());
  EXPECT_FALSE(terrain::remapForMove(3, 0, 3).has_value());
  EXPECT_FALSE(terrain::remapForMove(terrain::maxPaletteSize + 1, 0, 1).has_value());
}

TEST(TerrainMaterialRemap, RemapsDenseAndUniformChunksAndReturnsOnlyTheChangedOnes)
{
  terrain::TerrainVolume volume;
  const auto dense = mixedChunk();
  const auto uniformOne = terrain::TerrainChunk::uniform(-50, 1);
  const auto uniformFour = terrain::TerrainChunk::uniform(-50, 4);
  ASSERT_NE(dense, nullptr);
  volume.setChunk({ 0, 0, 0 }, dense);
  volume.setChunk({ 1, 0, 0 }, uniformOne);
  volume.setChunk({ 0, 0, 1 }, uniformFour);
  const auto denseHash = dense->hash();
  const auto revision = volume.revision();

  const auto edits = terrain::remapMaterials(volume, mapping(1, 7));

  // The volume and the chunks it published are untouched.
  EXPECT_EQ(volume.revision(), revision);
  EXPECT_EQ(volume.chunk({ 0, 0, 0 }), dense);
  EXPECT_EQ(dense->hash(), denseHash);
  EXPECT_EQ(dense->materialAtIndex(1), 1);

  ASSERT_EQ(edits.size(), 2u);
  EXPECT_EQ(edits[0].first, (ChunkCoord{ 0, 0, 0 }));
  EXPECT_EQ(edits[1].first, (ChunkCoord{ 1, 0, 0 }));

  const auto& remapped = edits[0].second;
  ASSERT_FALSE(remapped->isUniform());
  EXPECT_TRUE(std::ranges::equal(remapped->distances(), dense->distances()));
  for (int i = 0; i < terrain::samplesPerChunk; ++i)
  {
    const uint8_t was = dense->materialAtIndex(i);
    ASSERT_EQ(remapped->materialAtIndex(i), was == 1 ? 7 : was) << i;
  }

  ASSERT_TRUE(edits[1].second->isUniform());
  EXPECT_EQ(edits[1].second->distances().front(), -50);
  EXPECT_EQ(edits[1].second->materials().front(), 7);
}

TEST(TerrainMaterialRemap, RemapsAirSamplesAndLeavesAbsentChunksAbsent)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ 0, 0, 0 }, mixedChunk());

  const auto edits = terrain::remapMaterials(volume, mapping(2, 5));
  ASSERT_EQ(edits.size(), 1u);

  bool sawAir = false;
  for (int i = 0; i < terrain::samplesPerChunk; ++i)
  {
    if (edits[0].second->distanceAtIndex(i) > 0)
    {
      sawAir = true;
      ASSERT_EQ(edits[0].second->materialAtIndex(i), 5) << i;
    }
  }
  EXPECT_TRUE(sawAir);

  // Absent air reads material 0; remapping 0 has no chunk to rebuild.
  terrain::TerrainVolume empty;
  EXPECT_TRUE(terrain::remapMaterials(empty, mapping(0, 3)).empty());
}

TEST(TerrainMaterialRemap, AnIdentityOrInvalidRemapOrOneNoSampleUsesChangesNothing)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ 0, 0, 0 }, mixedChunk());

  EXPECT_FALSE(terrain::remapMaterials(volume, mapping(0, 9)).empty());
  EXPECT_TRUE(terrain::remapMaterials(volume, terrain::identityRemap()).empty());
  EXPECT_TRUE(terrain::remapMaterials(volume, mapping(0, terrain::reservedMaterial)).empty());
  EXPECT_TRUE(terrain::remapMaterials(volume, mapping(9, 0)).empty());
}

TEST(TerrainMaterialRemap, CommitRemapSetsTheChunksItReturns)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ 0, 0, 0 }, terrain::TerrainChunk::uniform(-50, 1));

  const auto edits = terrain::commitRemap(volume, mapping(1, 3));
  ASSERT_EQ(edits.size(), 1u);
  EXPECT_EQ(volume.chunk({ 0, 0, 0 }), edits[0].second);
  EXPECT_EQ(volume.materialAtSample({ 0, 0, 0 }), 3);
}

namespace {
  // A store holding one terrain with a palette of size entries.
  struct View
  {
    terrain::TerrainStore store;
    std::shared_ptr<terrain::TerrainVolume> volume;

    explicit View(const size_t size)
    {
      volume = store.create({ .uuid = terrainId, .palette = palette(size) });
    }
  };
}

TEST(TerrainMaterialRemap, PredictionRemapsTheViewAndItsCommandHoldsTheBeforeChunksAndPalettes)
{
  View view(8);
  const auto dense = mixedChunk();
  const auto untouched = terrain::TerrainChunk::uniform(-50, 4);
  view.volume->setChunk({ 0, 0, 0 }, dense);
  view.volume->setChunk({ 2, 0, 0 }, untouched);
  auto after = palette(8);
  after[6].name = "renamed";

  const auto prediction = edits::predictTerrainRemap(terrainId, view.store, mapping(1, 6), after, 9);
  ASSERT_TRUE(prediction.has_value());
  EXPECT_EQ(view.volume->materialAtSample({ 1, 0, 0 }), 6);
  EXPECT_EQ(view.volume->chunk({ 2, 0, 0 }), untouched);
  EXPECT_EQ(view.store.description(terrainId)->palette, after);

  const auto sent = terrain::unpackTerrainEdit(prediction->message);
  ASSERT_TRUE(sent.has_value());
  EXPECT_EQ(sent->kind, terrain::TerrainEditKind::remapMaterials);
  EXPECT_EQ(sent->terrain, terrainId);
  EXPECT_EQ(sent->strokeId, 9u);
  EXPECT_EQ(sent->remap, mapping(1, 6));
  EXPECT_EQ(sent->paletteHash, terrain::paletteHash(palette(8)));
  EXPECT_EQ(sent->palette, after);

  const auto* recorded = prediction->command.terrainStrokeChunks();
  ASSERT_NE(recorded, nullptr);
  ASSERT_EQ(recorded->size(), 1u);
  EXPECT_EQ((*recorded)[0].coord, (ChunkCoord{ 0, 0, 0 }));
  EXPECT_EQ((*recorded)[0].before, dense);
  EXPECT_EQ((*recorded)[0].after, view.volume->chunk({ 0, 0, 0 }));
}

TEST(TerrainMaterialRemap, PredictionRecordsAPaletteOnlyEntryWhenTheViewHoldsNothingToChange)
{
  View view(8);
  view.volume->setChunk({ 0, 0, 0 }, terrain::TerrainChunk::uniform(-50, 4));
  const auto revision = view.volume->revision();
  auto after = palette(8);
  after[1].name = "moved";

  const auto prediction = edits::predictTerrainRemap(terrainId, view.store, mapping(1, 6), after);
  ASSERT_TRUE(prediction.has_value());
  ASSERT_NE(prediction->command.terrainStrokeChunks(), nullptr);
  EXPECT_TRUE(prediction->command.terrainStrokeChunks()->empty());
  EXPECT_TRUE(terrain::unpackTerrainEdit(prediction->message).has_value());
  EXPECT_EQ(view.volume->revision(), revision);
  EXPECT_EQ(view.store.description(terrainId)->palette, after);
}

TEST(TerrainMaterialRemap, PredictionRefusesBeforeTouchingTheView)
{
  View view(8);
  view.volume->setChunk({ 0, 0, 0 }, terrain::TerrainChunk::uniform(-50, 1));
  const auto revision = view.volume->revision();
  auto after = palette(8);
  after[0].name = "changed";
  auto badColor = after;
  badColor[0].color.r = 2.0f;

  EXPECT_FALSE(edits::predictTerrainRemap(terrainId, view.store, terrain::identityRemap(), after).has_value());
  EXPECT_FALSE(
    edits::predictTerrainRemap(terrainId, view.store, mapping(4, terrain::reservedMaterial), after).has_value());
  EXPECT_FALSE(edits::predictTerrainRemap(terrainId, view.store, mapping(1, 6), palette(8)).has_value());
  EXPECT_FALSE(edits::predictTerrainRemap(terrainId, view.store, mapping(1, 6), badColor).has_value());
  const auto other = uuids::uuid::from_string("44444444-4444-4444-4444-444444444444").value();
  EXPECT_FALSE(edits::predictTerrainRemap(other, view.store, mapping(1, 6), after).has_value());
  EXPECT_EQ(view.volume->revision(), revision);
  EXPECT_EQ(view.store.description(terrainId)->palette, palette(8));

  EXPECT_TRUE(edits::predictTerrainRemap(terrainId, view.store, mapping(1, 6), after).has_value());
  EXPECT_NE(view.volume->revision(), revision);
}
