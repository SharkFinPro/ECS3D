#include <gtest/gtest.h>

#include "terrain/TerrainChunk.h"
#include "terrain/TerrainEdit.h"
#include "terrain/TerrainEditApply.h"
#include "terrain/TerrainGenerators.h"
#include "terrain/TerrainMaterialRemap.h"
#include "terrain/TerrainPalette.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainVolume.h"

#include <Protocol.h>
#include <ServerPolicy.h>
#include <glm/vec3.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <uuid.h>

namespace {
  const auto terrainId = uuids::uuid::from_string("77777777-7777-4777-8777-777777777777").value();
  const auto textureId = uuids::uuid::from_string("88888888-8888-4888-8888-888888888888").value();

  std::vector<terrain::TerrainMaterial> paletteOf(const size_t count)
  {
    std::vector<terrain::TerrainMaterial> palette;

    for (size_t i = 0; i < count; ++i)
    {
      palette.push_back({ .name = "m" + std::to_string(i), .color = glm::vec3(0.5f) });
    }

    return palette;
  }

  terrain::MaterialRemap swapRemap(const uint8_t first, const uint8_t second)
  {
    auto remap = terrain::identityRemap();
    remap[first] = second;
    remap[second] = first;
    return remap;
  }

  // A terrain whose ground is material 1, with a three-entry palette.
  struct Env
  {
    terrain::TerrainStore store;
    std::shared_ptr<terrain::TerrainVolume> volume;

    Env()
    {
      volume = store.create({ .uuid = terrainId, .voxelSize = 2.5f, .palette = paletteOf(3) });

      const uint32_t revision = store.nextRevision(terrainId);
      for (const auto& [coord, chunk] : terrain::fillFlat(*volume, { 64, 64 }, 10.0f, 20.0f, 1))
      {
        store.setChunk(terrainId, coord, chunk, revision);
      }
    }

    [[nodiscard]] terrain::TerrainEditPayload swap(std::vector<terrain::TerrainMaterial> palette) const
    {
      terrain::TerrainEditPayload edit;
      edit.terrain = terrainId;
      edit.kind = terrain::TerrainEditKind::remapMaterials;
      edit.remap = swapRemap(1, 2);
      edit.paletteHash = terrain::paletteHash(store.description(terrainId)->palette);
      edit.palette = std::move(palette);
      return edit;
    }

    [[nodiscard]] std::vector<terrain::TerrainMaterial> swappedPalette() const
    {
      auto palette = store.description(terrainId)->palette;
      std::swap(palette[1], palette[2]);
      return palette;
    }
  };

  bool accepted(const net::Message& message)
  {
    return terrain::unpackTerrainEdit(message).has_value();
  }
}

TEST(TerrainPaletteHash, EqualPalettesHashEquallyAndEveryFieldChangesTheHash)
{
  const auto base = paletteOf(3);
  EXPECT_EQ(terrain::paletteHash(base), terrain::paletteHash(paletteOf(3)));

  std::vector<std::vector<terrain::TerrainMaterial>> variants(6, base);
  variants[0][1].name = "other";
  variants[1][1].albedoTexture = textureId;
  variants[2][1].specularTexture = textureId;
  variants[3][1].color.g = 0.25f;
  variants[4].pop_back();
  std::swap(variants[5][0], variants[5][1]);

  for (const auto& variant : variants)
  {
    EXPECT_NE(terrain::paletteHash(variant), terrain::paletteHash(base));
  }

  // Names are length-prefixed, so moving a character from one name to the next is a different palette.
  auto shifted = base;
  shifted[0].name = "m0m";
  shifted[1].name = "1";
  EXPECT_NE(terrain::paletteHash(shifted), terrain::paletteHash(base));
}

TEST(TerrainRemapEdit, RoundTripsTheTableTheHashAndThePalette)
{
  auto palette = paletteOf(2);
  palette[1].albedoTexture = textureId;
  palette[0].color = glm::vec3(0.25f, 0.5f, 0.75f);

  const auto message = terrain::packRemapEdit(terrainId, 5, swapRemap(0, 1), 0x1122334455667788ull, palette);
  const auto payload = terrain::unpackTerrainEdit(message);

  ASSERT_TRUE(payload.has_value());
  EXPECT_EQ(payload->kind, terrain::TerrainEditKind::remapMaterials);
  EXPECT_EQ(payload->terrain, terrainId);
  EXPECT_EQ(payload->strokeId, 5u);
  EXPECT_EQ(payload->remap, swapRemap(0, 1));
  EXPECT_EQ(payload->paletteHash, 0x1122334455667788ull);
  EXPECT_EQ(payload->palette, palette);
}

TEST(TerrainRemapEdit, RefusesATableThatMovesTheReservedIndexTheIdentityAndAnOversizedPalette)
{
  EXPECT_TRUE(accepted(terrain::packRemapEdit(terrainId, 1, swapRemap(0, 1), 0, paletteOf(16))));

  auto reservedElsewhere = swapRemap(0, 1);
  reservedElsewhere[terrain::reservedMaterial] = 0;
  EXPECT_FALSE(accepted(terrain::packRemapEdit(terrainId, 1, reservedElsewhere, 0, paletteOf(2))));

  auto ontoReserved = swapRemap(0, 1);
  ontoReserved[7] = terrain::reservedMaterial;
  EXPECT_FALSE(accepted(terrain::packRemapEdit(terrainId, 1, ontoReserved, 0, paletteOf(2))));

  EXPECT_FALSE(accepted(terrain::packRemapEdit(terrainId, 1, terrain::identityRemap(), 0, paletteOf(2))));
  EXPECT_THROW(static_cast<void>(terrain::packRemapEdit(terrainId, 1, swapRemap(0, 1), 0, paletteOf(17))),
               std::length_error);

  // The count is read before the entries, so a count over the limit is refused whatever follows it.
  const auto empty = terrain::packRemapEdit(terrainId, 1, swapRemap(0, 1), 0, {});
  std::vector<uint8_t> bytes(empty.bytes().begin(), empty.bytes().end());
  bytes[bytes.size() - 4] = 17;
  EXPECT_FALSE(accepted(net::Message(net::MessageType::terrainEdit, bytes)));
  bytes[bytes.size() - 4] = 0;
  EXPECT_TRUE(accepted(net::Message(net::MessageType::terrainEdit, bytes)));
}

TEST(TerrainRemapApply, RemapsTheChunksAndReplacesThePaletteInOneStep)
{
  Env env;
  const auto expected = terrain::remapMaterials(*env.volume, swapRemap(1, 2));
  ASSERT_FALSE(expected.empty());
  const uint32_t before = env.store.nextRevision(terrainId);
  const auto palette = env.swappedPalette();

  const auto result = terrain::applyTerrainEdit(env.store, env.swap(palette));

  EXPECT_EQ(result.refusal, terrain::TerrainEditRefusal::none);
  EXPECT_TRUE(result.paletteReplaced);
  EXPECT_EQ(result.revision, before + 1);
  EXPECT_EQ(result.changed.size(), expected.size());
  EXPECT_EQ(env.store.description(terrainId)->palette, palette);
  EXPECT_EQ(env.volume->materialAtSample({ 0, 0, 0 }), 2);
  for (const auto& [coord, chunk] : expected)
  {
    EXPECT_EQ(env.volume->chunk(coord)->hash(), chunk->hash());
    EXPECT_EQ(env.store.revision(terrainId, coord), result.revision);
  }
  EXPECT_EQ(env.store.nextRevision(terrainId), before + 2);
}

TEST(TerrainRemapApply, RefusesAPaletteHashMismatchAndChangesNothing)
{
  Env env;
  const auto chunks = env.volume->snapshotChunks();
  const uint32_t before = env.store.nextRevision(terrainId);

  auto stale = env.swap(env.swappedPalette());
  stale.paletteHash ^= 1;
  const auto result = terrain::applyTerrainEdit(env.store, stale);

  EXPECT_EQ(result.refusal, terrain::TerrainEditRefusal::paletteChanged);
  EXPECT_FALSE(result.paletteReplaced);
  EXPECT_TRUE(result.changed.empty());
  EXPECT_EQ(env.volume->snapshotChunks(), chunks);
  EXPECT_EQ(env.store.description(terrainId)->palette, paletteOf(3));
  EXPECT_EQ(env.store.nextRevision(terrainId), before + 1);

  // The same edit under the right hash applies.
  EXPECT_EQ(terrain::applyTerrainEdit(env.store, env.swap(env.swappedPalette())).refusal,
            terrain::TerrainEditRefusal::none);
  EXPECT_NE(env.volume->snapshotChunks(), chunks);
}

TEST(TerrainRemapApply, RefusesAnInvalidPaletteOrRemapAndAnUnknownTerrain)
{
  Env env;
  const auto chunks = env.volume->snapshotChunks();

  auto badColor = env.swappedPalette();
  badColor[0].color.r = 2.0f;
  EXPECT_EQ(terrain::applyTerrainEdit(env.store, env.swap(badColor)).refusal, terrain::TerrainEditRefusal::invalidOp);

  auto identity = env.swap(env.swappedPalette());
  identity.remap = terrain::identityRemap();
  EXPECT_EQ(terrain::applyTerrainEdit(env.store, identity).refusal, terrain::TerrainEditRefusal::invalidOp);

  auto unknown = env.swap(env.swappedPalette());
  unknown.terrain = textureId;
  EXPECT_EQ(terrain::applyTerrainEdit(env.store, unknown).refusal, terrain::TerrainEditRefusal::unknownTerrain);

  EXPECT_EQ(env.volume->snapshotChunks(), chunks);
  EXPECT_EQ(env.store.description(terrainId)->palette, paletteOf(3));
  EXPECT_EQ(terrain::applyTerrainEdit(env.store, env.swap(env.swappedPalette())).refusal,
            terrain::TerrainEditRefusal::none);
}

TEST(TerrainRemapApply, ARemapNoSampleUsesStillReplacesThePaletteAndTakesNoRevision)
{
  Env env;
  const auto chunks = env.volume->snapshotChunks();
  const uint32_t before = env.store.nextRevision(terrainId);

  auto palette = paletteOf(3);
  std::swap(palette[0], palette[2]);
  auto edit = env.swap(palette);
  edit.remap = swapRemap(5, 6);
  const auto result = terrain::applyTerrainEdit(env.store, edit);

  EXPECT_EQ(result.refusal, terrain::TerrainEditRefusal::none);
  EXPECT_TRUE(result.paletteReplaced);
  EXPECT_TRUE(result.changed.empty());
  EXPECT_EQ(result.revision, 0u);
  EXPECT_EQ(env.volume->snapshotChunks(), chunks);
  EXPECT_EQ(env.store.description(terrainId)->palette, edit.palette);
  EXPECT_EQ(env.store.nextRevision(terrainId), before + 1);
}

TEST(TerrainRemapApply, OnlyAPaletteMismatchSendsTheSenderTheTerrainAgain)
{
  EXPECT_TRUE(terrainEditRefusalNeedsResync(terrain::TerrainEditRefusal::paletteChanged));

  for (const auto refusal : { terrain::TerrainEditRefusal::none, terrain::TerrainEditRefusal::unknownTerrain,
                              terrain::TerrainEditRefusal::invalidOp, terrain::TerrainEditRefusal::tooLarge,
                              terrain::TerrainEditRefusal::revisionsExhausted })
  {
    EXPECT_FALSE(terrainEditRefusalNeedsResync(refusal));
  }
}

TEST(TerrainRemapApply, ARefusedRemapRestampsOnlyTheChunksItWouldHaveChanged)
{
  Env env;
  const auto chunks = env.volume->snapshotChunks();
  const auto expected = terrain::remapMaterials(*env.volume, swapRemap(1, 2));
  ASSERT_FALSE(expected.empty());
  const uint32_t before = env.store.nextRevision(terrainId);

  auto refused = env.swap(env.swappedPalette());
  refused.paletteHash ^= 1;
  const auto coords = terrain::restampRefusedRemap(env.store, refused);

  ASSERT_EQ(coords.size(), expected.size());
  for (size_t i = 0; i < coords.size(); ++i)
  {
    EXPECT_EQ(coords[i], expected[i].first);
    EXPECT_EQ(env.store.revision(terrainId, coords[i]), before + 1);
  }
  EXPECT_EQ(env.volume->snapshotChunks(), chunks);
  EXPECT_EQ(env.store.nextRevision(terrainId), before + 2);

  // Nothing to repair: a remap no sample uses, another kind or an unknown terrain stamps nothing.
  auto unused = refused;
  unused.remap = swapRemap(5, 6);
  EXPECT_TRUE(terrain::restampRefusedRemap(env.store, unused).empty());
  auto brush = refused;
  brush.kind = terrain::TerrainEditKind::brush;
  EXPECT_TRUE(terrain::restampRefusedRemap(env.store, brush).empty());
  auto unknown = refused;
  unknown.terrain = textureId;
  EXPECT_TRUE(terrain::restampRefusedRemap(env.store, unknown).empty());
  EXPECT_EQ(env.store.nextRevision(terrainId), before + 3);
}

TEST(TerrainPaletteHash, NegativeZeroHashesAsZero)
{
  auto negative = paletteOf(2);
  negative[0].color = glm::vec3(-0.0f, 0.5f, 0.5f);
  auto positive = paletteOf(2);
  positive[0].color = glm::vec3(0.0f, 0.5f, 0.5f);

  EXPECT_EQ(terrain::paletteHash(negative), terrain::paletteHash(positive));
  positive[0].color.g = 0.0f;
  EXPECT_NE(terrain::paletteHash(negative), terrain::paletteHash(positive));
}
