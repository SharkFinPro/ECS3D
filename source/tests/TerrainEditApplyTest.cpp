#include <gtest/gtest.h>

#include "terrain/TerrainBrush.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainEdit.h"
#include "terrain/TerrainEditApply.h"
#include "terrain/TerrainGenerators.h"
#include "terrain/TerrainOps.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

namespace {
  constexpr float voxel = 2.5f;
  const terrain::SampleBox flattenBox{ { 10, -4, 10 }, { 40, 24, 40 } };

  uuids::uuid uuidFrom(const char* text)
  {
    return uuids::uuid::from_string(text).value();
  }

  const auto terrainId = uuidFrom("11111111-1111-1111-1111-111111111111");
  const auto otherId = uuidFrom("22222222-2222-2222-2222-222222222222");

  uint64_t hashOf(const std::shared_ptr<const terrain::TerrainChunk>& chunk)
  {
    static const auto air = terrain::TerrainChunk::uniform(terrain::airDistance, 0);
    return (chunk ? chunk : air)->hash();
  }

  struct Env
  {
    terrain::TerrainStore store;
    std::shared_ptr<terrain::TerrainVolume> volume;

    explicit Env(const bool flattened)
    {
      terrain::TerrainDescription description;
      description.uuid = terrainId;
      description.voxelSize = voxel;
      volume = store.create(description);

      const uint32_t revision = store.nextRevision(terrainId);
      for (const auto& [coord, chunk] : terrain::fillFlat(*volume, { 64, 64 }, 10.0f, 20.0f, 1))
      {
        store.setChunk(terrainId, coord, chunk, revision);
      }

      if (flattened)
      {
        terrain::commit(*volume, terrain::editSamples(*volume, flattenBox,
                                                      [](glm::ivec3, int16_t& distance, uint8_t&)
        {
          distance = static_cast<int16_t>(std::lround(static_cast<float>(distance) * 0.4f));
        }));
      }
    }

    // Consumes one revision and returns it, so it is the highest handed out so far.
    uint32_t counter()
    {
      return store.nextRevision(terrainId);
    }
  };

  struct State
  {
    terrain::TerrainVolume::ChunkMap chunks;
    uint64_t volumeRevision = 0;
    std::vector<uint32_t> revisions;
  };

  State capture(const Env& env)
  {
    State state;
    state.chunks = env.volume->snapshotChunks();
    state.volumeRevision = env.volume->revision();

    for (int z = -2; z <= 5; ++z)
    {
      for (int y = -4; y <= 4; ++y)
      {
        for (int x = -2; x <= 5; ++x)
        {
          state.revisions.push_back(env.store.revision(terrainId, { x, y, z }));
        }
      }
    }

    return state;
  }

  void expectSame(const State& before, const State& after)
  {
    EXPECT_EQ(before.chunks, after.chunks);
    EXPECT_EQ(before.volumeRevision, after.volumeRevision);
    EXPECT_EQ(before.revisions, after.revisions);
  }

  terrain::BrushOp addBrush(const glm::vec3 center, const float radius)
  {
    terrain::BrushOp op;
    op.kind = terrain::BrushOp::Kind::add;
    op.center = center;
    op.radius = radius;
    op.material = 7;
    return op;
  }

  terrain::BrushOp subtractBrush(const glm::vec3 center, const float radius)
  {
    auto op = addBrush(center, radius);
    op.kind = terrain::BrushOp::Kind::subtract;
    return op;
  }

  terrain::TerrainEditPayload brushEdit(const terrain::BrushOp& op, const uuids::uuid& id = terrainId)
  {
    terrain::TerrainEditPayload edit;
    edit.terrain = id;
    edit.kind = terrain::TerrainEditKind::brush;
    edit.brush = op;
    return edit;
  }

  terrain::TerrainEditPayload redistanceEdit(const terrain::SampleBox& box)
  {
    terrain::TerrainEditPayload edit;
    edit.terrain = terrainId;
    edit.kind = terrain::TerrainEditKind::redistance;
    edit.box = box;
    return edit;
  }

  terrain::TerrainEditPayload setChunksEdit(std::vector<terrain::SetChunkEntry> chunks)
  {
    terrain::TerrainEditPayload edit;
    edit.terrain = terrainId;
    edit.kind = terrain::TerrainEditKind::setChunks;
    edit.chunks = std::move(chunks);
    return edit;
  }

  std::vector<terrain::ChunkCoord> coordsOf(const terrain::ChunkEdits& edits)
  {
    std::vector<terrain::ChunkCoord> coords;

    for (const auto& edit : edits)
    {
      coords.push_back(edit.first);
    }

    return coords;
  }

  void expectApplied(Env& env, const terrain::ChunkEdits& expected, const terrain::TerrainEditResult& result,
                     const uint32_t previousCounter)
  {
    ASSERT_FALSE(expected.empty());
    EXPECT_EQ(result.refusal, terrain::TerrainEditRefusal::none);
    EXPECT_EQ(result.changed, coordsOf(expected));
    EXPECT_GT(result.revision, previousCounter);

    for (const auto& [coord, chunk] : expected)
    {
      EXPECT_EQ(hashOf(env.volume->chunk(coord)), hashOf(chunk));
      EXPECT_EQ(env.store.revision(terrainId, coord), result.revision);
    }
  }

  void expectRefused(Env& env, const terrain::TerrainEditPayload& edit, const terrain::TerrainEditRefusal refusal)
  {
    const uint32_t before = env.counter();
    const State state = capture(env);

    const auto result = terrain::applyTerrainEdit(env.store, edit);

    EXPECT_EQ(result.refusal, refusal);
    EXPECT_EQ(result.revision, 0u);
    EXPECT_TRUE(result.changed.empty());
    expectSame(state, capture(env));
    EXPECT_EQ(env.counter(), before + 1);
  }

  std::shared_ptr<const terrain::TerrainChunk> clone(const terrain::TerrainChunk& chunk)
  {
    if (chunk.isUniform())
    {
      return terrain::TerrainChunk::uniform(chunk.distances()[0], chunk.materials()[0]);
    }

    return terrain::TerrainChunk::dense({ chunk.distances().begin(), chunk.distances().end() },
                                        { chunk.materials().begin(), chunk.materials().end() });
  }
}

TEST(TerrainEditApply, BrushChangesTheChunksApplyBrushReportsUnderOneFreshRevision)
{
  Env env(false);
  const auto op = addBrush({ 80.0f, 10.0f, 80.0f }, 10.0f);
  const auto expected = terrain::applyBrush(*env.volume, op);
  const uint64_t volumeBefore = env.volume->revision();
  const uint32_t before = env.counter();

  const auto result = terrain::applyTerrainEdit(env.store, brushEdit(op));

  expectApplied(env, expected, result, before);
  EXPECT_EQ(result.revision, before + 1);
  EXPECT_NE(env.volume->revision(), volumeBefore);
  EXPECT_EQ(env.counter(), before + 2);
}

TEST(TerrainEditApply, TwoEditsInARowTakeIncreasingRevisions)
{
  Env env(false);
  const auto first = terrain::applyTerrainEdit(env.store, brushEdit(addBrush({ 80.0f, 10.0f, 80.0f }, 10.0f)));
  const auto second = terrain::applyTerrainEdit(env.store, brushEdit(addBrush({ 40.0f, 10.0f, 40.0f }, 10.0f)));

  EXPECT_EQ(first.refusal, terrain::TerrainEditRefusal::none);
  EXPECT_EQ(second.refusal, terrain::TerrainEditRefusal::none);
  EXPECT_FALSE(first.changed.empty());
  EXPECT_FALSE(second.changed.empty());
  EXPECT_EQ(second.revision, first.revision + 1);
}

TEST(TerrainEditApply, BrushThatChangesNothingTakesNoRevision)
{
  Env env(false);
  const auto farAway = subtractBrush({ 80.0f, 400.0f, 80.0f }, 10.0f);
  ASSERT_TRUE(terrain::applyBrush(*env.volume, farAway).empty());
  const uint32_t before = env.counter();

  const auto result = terrain::applyTerrainEdit(env.store, brushEdit(farAway));

  EXPECT_EQ(result.refusal, terrain::TerrainEditRefusal::none);
  EXPECT_EQ(result.revision, 0u);
  EXPECT_TRUE(result.changed.empty());
  EXPECT_EQ(env.counter(), before + 1);

  const auto control = terrain::applyTerrainEdit(env.store, brushEdit(addBrush({ 80.0f, 10.0f, 80.0f }, 10.0f)));
  EXPECT_FALSE(control.changed.empty());
}

TEST(TerrainEditApply, RedistanceChangesTheChunksRedistanceReports)
{
  Env env(true);
  const auto expected = terrain::redistance(*env.volume, flattenBox);
  const uint32_t before = env.counter();

  const auto result = terrain::applyTerrainEdit(env.store, redistanceEdit(flattenBox));

  expectApplied(env, expected, result, before);
  EXPECT_EQ(result.revision, before + 1);
}

TEST(TerrainEditApply, RedistanceOverACleanFieldChangesNothingAndTakesNoRevision)
{
  Env clean(false);
  ASSERT_TRUE(terrain::redistance(*clean.volume, flattenBox).empty());
  const uint32_t before = clean.counter();
  const State state = capture(clean);

  const auto result = terrain::applyTerrainEdit(clean.store, redistanceEdit(flattenBox));

  EXPECT_EQ(result.refusal, terrain::TerrainEditRefusal::none);
  EXPECT_EQ(result.revision, 0u);
  EXPECT_TRUE(result.changed.empty());
  expectSame(state, capture(clean));
  EXPECT_EQ(clean.counter(), before + 1);

  Env dirty(true);
  EXPECT_FALSE(terrain::applyTerrainEdit(dirty.store, redistanceEdit(flattenBox)).changed.empty());
}

TEST(TerrainEditApply, SetChunksAppliesADifferentChunkAndDropsAnIdenticalOne)
{
  Env env(false);
  const auto held = env.volume->chunk({ 0, 0, 0 });
  ASSERT_NE(held, nullptr);
  const auto different = terrain::TerrainChunk::uniform(-terrain::airDistance, 5);
  ASSERT_NE(different->hash(), held->hash());

  const uint32_t before = env.counter();
  const auto result = terrain::applyTerrainEdit(env.store, setChunksEdit({ { { 0, 0, 0 }, different } }));

  EXPECT_EQ(result.refusal, terrain::TerrainEditRefusal::none);
  ASSERT_EQ(result.changed.size(), 1u);
  EXPECT_EQ(result.changed[0], (terrain::ChunkCoord{ 0, 0, 0 }));
  EXPECT_EQ(result.revision, before + 1);
  EXPECT_EQ(env.volume->chunk({ 0, 0, 0 })->hash(), different->hash());
  EXPECT_EQ(env.store.revision(terrainId, { 0, 0, 0 }), result.revision);

  const auto identical = clone(*env.volume->chunk({ 0, 0, 0 }));
  const uint32_t beforeIdentical = env.counter();
  const State state = capture(env);
  const auto dropped = terrain::applyTerrainEdit(env.store, setChunksEdit({ { { 0, 0, 0 }, identical } }));

  EXPECT_EQ(dropped.refusal, terrain::TerrainEditRefusal::none);
  EXPECT_EQ(dropped.revision, 0u);
  EXPECT_TRUE(dropped.changed.empty());
  expectSame(state, capture(env));
  EXPECT_EQ(env.counter(), beforeIdentical + 1);
}

TEST(TerrainEditApply, SetChunksKeepsOnlyTheEntriesThatDiffer)
{
  Env env(false);
  const auto same = clone(*env.volume->chunk({ 0, 0, 0 }));
  const auto different = terrain::TerrainChunk::uniform(-terrain::airDistance, 5);
  const uint32_t heldRevision = env.store.revision(terrainId, { 0, 0, 0 });

  const auto result =
    terrain::applyTerrainEdit(env.store, setChunksEdit({ { { 0, 0, 0 }, same }, { { 1, 0, 0 }, different } }));

  ASSERT_EQ(result.changed.size(), 1u);
  EXPECT_EQ(result.changed[0], (terrain::ChunkCoord{ 1, 0, 0 }));
  EXPECT_EQ(env.store.revision(terrainId, { 0, 0, 0 }), heldRevision);
  EXPECT_EQ(env.store.revision(terrainId, { 1, 0, 0 }), result.revision);
}

TEST(TerrainEditApply, ANullEntryErasesAHeldChunkAndIsANoOpOverAbsentAir)
{
  Env env(false);
  ASSERT_NE(env.volume->chunk({ 0, 0, 0 }), nullptr);
  ASSERT_EQ(env.volume->chunk({ 50, 50, 50 }), nullptr);

  const uint32_t before = env.counter();
  const State state = capture(env);
  const auto airOverAir = terrain::TerrainChunk::uniform(terrain::airDistance, 0);

  const auto noop = terrain::applyTerrainEdit(
    env.store, setChunksEdit({ { { 50, 50, 50 }, nullptr }, { { 51, 50, 50 }, airOverAir } }));

  EXPECT_EQ(noop.refusal, terrain::TerrainEditRefusal::none);
  EXPECT_EQ(noop.revision, 0u);
  EXPECT_TRUE(noop.changed.empty());
  expectSame(state, capture(env));
  EXPECT_EQ(env.counter(), before + 1);

  const auto erased = terrain::applyTerrainEdit(env.store, setChunksEdit({ { { 0, 0, 0 }, nullptr } }));

  ASSERT_EQ(erased.changed.size(), 1u);
  EXPECT_EQ(erased.changed[0], (terrain::ChunkCoord{ 0, 0, 0 }));
  EXPECT_EQ(env.volume->chunk({ 0, 0, 0 }), nullptr);
  EXPECT_EQ(env.store.revision(terrainId, { 0, 0, 0 }), erased.revision);
}

TEST(TerrainEditApply, AirOfAnyMaterialOverAbsentAirIsANoOpAndErasesAHeldChunk)
{
  Env env(false);
  ASSERT_EQ(env.volume->chunk({ 50, 50, 50 }), nullptr);
  ASSERT_NE(env.volume->chunk({ 0, 0, 0 }), nullptr);
  const auto airWithMaterial = terrain::TerrainChunk::uniform(terrain::airDistance, 5);
  ASSERT_TRUE(airWithMaterial->isAir());

  const uint32_t before = env.counter();
  const State state = capture(env);
  const auto noop = terrain::applyTerrainEdit(env.store, setChunksEdit({ { { 50, 50, 50 }, airWithMaterial } }));

  EXPECT_EQ(noop.refusal, terrain::TerrainEditRefusal::none);
  EXPECT_EQ(noop.revision, 0u);
  EXPECT_TRUE(noop.changed.empty());
  expectSame(state, capture(env));
  EXPECT_EQ(env.counter(), before + 1);

  const auto erased = terrain::applyTerrainEdit(env.store, setChunksEdit({ { { 0, 0, 0 }, airWithMaterial } }));

  ASSERT_EQ(erased.changed.size(), 1u);
  EXPECT_EQ(erased.changed[0], (terrain::ChunkCoord{ 0, 0, 0 }));
  EXPECT_EQ(env.volume->chunk({ 0, 0, 0 }), nullptr);
  EXPECT_EQ(env.store.revision(terrainId, { 0, 0, 0 }), erased.revision);
}

TEST(TerrainEditApply, RefusesAnUnknownTerrain)
{
  Env env(false);
  const auto op = addBrush({ 80.0f, 10.0f, 80.0f }, 10.0f);

  expectRefused(env, brushEdit(op, otherId), terrain::TerrainEditRefusal::unknownTerrain);
  EXPECT_FALSE(terrain::applyTerrainEdit(env.store, brushEdit(op)).changed.empty());
}

TEST(TerrainEditApply, RefusesABrushTheVolumeCannotTrust)
{
  Env env(false);
  const float limit = terrain::maxBrushRadiusVoxels * voxel;

  expectRefused(env, brushEdit(addBrush({ 80.0f, 10.0f, 80.0f }, limit * 1.01f)),
                terrain::TerrainEditRefusal::invalidOp);
  expectRefused(env, brushEdit(addBrush({ std::numeric_limits<float>::quiet_NaN(), 10.0f, 80.0f }, 10.0f)),
                terrain::TerrainEditRefusal::invalidOp);
  expectRefused(env, brushEdit(addBrush({ std::numeric_limits<float>::max(), 10.0f, 80.0f }, 10.0f)),
                terrain::TerrainEditRefusal::invalidOp);

  EXPECT_FALSE(terrain::applyTerrainEdit(env.store, brushEdit(addBrush({ 80.0f, 10.0f, 80.0f }, 10.0f)))
                 .changed.empty());
}

TEST(TerrainEditApply, RefusesRedistanceBoxesItCannotBound)
{
  Env env(true);
  constexpr int32_t edge = (1 << 29) + 30;

  expectRefused(env, redistanceEdit({ { 5, 5, 5 }, { 5, 9, 9 } }), terrain::TerrainEditRefusal::invalidOp);
  expectRefused(env, redistanceEdit({ { edge, 0, 0 }, { edge + 10, 8, 8 } }), terrain::TerrainEditRefusal::invalidOp);
  expectRefused(env, redistanceEdit({ { -edge - 10, 0, 0 }, { -edge, 8, 8 } }),
                terrain::TerrainEditRefusal::invalidOp);
  expectRefused(env, redistanceEdit({ { 0, 0, 0 }, { 32 * 200, 32, 32 * 100 } }),
                terrain::TerrainEditRefusal::tooLarge);

  EXPECT_FALSE(terrain::applyTerrainEdit(env.store, redistanceEdit(flattenBox)).changed.empty());
}

TEST(TerrainEditApply, RefusesABoxSpanningTheWholeInt32RangeWithoutOverflow)
{
  Env env(true);
  constexpr int32_t low = std::numeric_limits<int32_t>::min();
  constexpr int32_t high = std::numeric_limits<int32_t>::max();

  expectRefused(env, redistanceEdit({ { low, low, low }, { high, high, high } }),
                terrain::TerrainEditRefusal::tooLarge);
  expectRefused(env, redistanceEdit({ { low, 0, 0 }, { high, 8, 8 } }), terrain::TerrainEditRefusal::tooLarge);
  expectRefused(env, redistanceEdit({ { 0, low, 0 }, { 8, high, 8 } }), terrain::TerrainEditRefusal::tooLarge);

  EXPECT_FALSE(terrain::applyTerrainEdit(env.store, redistanceEdit(flattenBox)).changed.empty());
}

TEST(TerrainEditApply, AGrownBoxJustOverTheRedistanceLimitIsRefusedAndOneAtTheLimitIsAccepted)
{
  static_assert(terrain::maxRedistanceSamples == 160ll * 160ll * 160ll);

  Env env(true);
  const terrain::SampleBox atLimit{ { 0, -70, 0 }, { 158, 88, 158 } };
  const terrain::SampleBox overLimit{ { 0, -70, 0 }, { 159, 88, 158 } };

  expectRefused(env, redistanceEdit(overLimit), terrain::TerrainEditRefusal::tooLarge);

  const auto accepted = terrain::applyTerrainEdit(env.store, redistanceEdit(atLimit));
  EXPECT_EQ(accepted.refusal, terrain::TerrainEditRefusal::none);
  EXPECT_FALSE(accepted.changed.empty());
}

TEST(TerrainEditApply, ABoxOverTheChunkLimitButWithinTheSampleLimitIsRefused)
{
  static_assert(terrain::maxEditChunks == 16384);

  Env env(true);
  // Grown samples: 250002 * 4 * 4 = 4000032, within maxRedistanceSamples (4096000). Chunks: 7813 * 2 * 2 = 31252.
  const terrain::SampleBox tooManyChunks{ { 0, 31, 31 }, { 250000, 33, 33 } };
  // Chunks: 4096 * 2 * 2 = 16384, exactly maxEditChunks.
  const terrain::SampleBox atChunkLimit{ { 0, 31, 31 }, { 131072, 33, 33 } };

  const int64_t grownSamples = (250000ll + 2) * (2 + 2) * (2 + 2);
  ASSERT_LE(grownSamples, terrain::maxRedistanceSamples);

  expectRefused(env, redistanceEdit(tooManyChunks), terrain::TerrainEditRefusal::tooLarge);

  EXPECT_EQ(terrain::applyTerrainEdit(env.store, redistanceEdit(atChunkLimit)).refusal,
            terrain::TerrainEditRefusal::none);
}

TEST(TerrainEditApply, RefusesASetChunksCoordinateBeyondTheLimit)
{
  Env env(false);
  const auto different = terrain::TerrainChunk::uniform(-terrain::airDistance, 5);

  expectRefused(env, setChunksEdit({ { { 1, 0, 0 }, different }, { { terrain::maxChunkCoord + 1, 0, 0 }, different } }),
                terrain::TerrainEditRefusal::invalidOp);

  EXPECT_FALSE(terrain::applyTerrainEdit(env.store, setChunksEdit({ { { 1, 0, 0 }, different } })).changed.empty());
}

TEST(TerrainEditApply, RefusesAChangingEditWhenRevisionsAreExhausted)
{
  Env env(false);
  const auto solid = terrain::TerrainChunk::uniform(-terrain::airDistance, 1);
  ASSERT_TRUE(env.store.setChunk(terrainId, { 40, 0, 0 }, solid, std::numeric_limits<uint32_t>::max()));
  ASSERT_EQ(env.store.nextRevision(terrainId), 0u);
  const State state = capture(env);

  const auto op = addBrush({ 80.0f, 10.0f, 80.0f }, 10.0f);
  ASSERT_FALSE(terrain::applyBrush(*env.volume, op).empty());
  const auto result = terrain::applyTerrainEdit(env.store, brushEdit(op));

  EXPECT_EQ(result.refusal, terrain::TerrainEditRefusal::revisionsExhausted);
  EXPECT_EQ(result.revision, 0u);
  EXPECT_TRUE(result.changed.empty());
  expectSame(state, capture(env));

  const auto unchanged =
    terrain::applyTerrainEdit(env.store, brushEdit(subtractBrush({ 80.0f, 400.0f, 80.0f }, 10.0f)));
  EXPECT_EQ(unchanged.refusal, terrain::TerrainEditRefusal::none);
}

TEST(TerrainEditApply, EveryRefusalHasItsOwnText)
{
  std::set<std::string_view> texts;

  for (const auto refusal : { terrain::TerrainEditRefusal::none, terrain::TerrainEditRefusal::unknownTerrain,
                              terrain::TerrainEditRefusal::invalidOp, terrain::TerrainEditRefusal::tooLarge,
                              terrain::TerrainEditRefusal::revisionsExhausted,
                              terrain::TerrainEditRefusal::paletteChanged })
  {
    const std::string_view text = terrain::refusalText(refusal);
    EXPECT_FALSE(text.empty());
    texts.insert(text);
  }

  EXPECT_EQ(texts.size(), 6u);
}
