#include <gtest/gtest.h>

#include "TerrainSurfaceFeed.h"
#include "TerrainSurfacePlan.h"
#include "TerrainSurfaceSampling.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"
#include "TerrainVolumeSource.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace {
  using terrain::ChunkCoord;
  using terrain::SampleBox;

  std::shared_ptr<const terrain::TerrainChunk> solid(const uint8_t material = 1)
  {
    return terrain::TerrainChunk::uniform(-terrain::airDistance, material);
  }

  // Every sample is distinguishable by its local coordinates.
  std::shared_ptr<const terrain::TerrainChunk> patterned()
  {
    std::vector<int16_t> distances(terrain::samplesPerChunk);
    std::vector<uint8_t> materials(terrain::samplesPerChunk);

    for (int z = 0; z < terrain::chunkSize; ++z)
    {
      for (int y = 0; y < terrain::chunkSize; ++y)
      {
        for (int x = 0; x < terrain::chunkSize; ++x)
        {
          const int index = terrain::sampleIndex(x, y, z);
          distances[static_cast<size_t>(index)] =
            static_cast<int16_t>((x + 3 * y + 7 * z) * 100 % (2 * terrain::airDistance) - terrain::airDistance);
          materials[static_cast<size_t>(index)] = static_cast<uint8_t>((x + 3 * y + 7 * z) % 250);
        }
      }
    }

    return terrain::TerrainChunk::dense(std::move(distances), std::move(materials));
  }

  void expectMatchesVolume(const terrain::TerrainVolume& volume, const SampleBox& region,
                           const std::vector<float>& distance, const std::vector<uint8_t>& material)
  {
    const glm::ivec3 size = region.max - region.min;
    size_t index = 0;

    for (int z = 0; z < size.z; ++z)
    {
      for (int y = 0; y < size.y; ++y)
      {
        for (int x = 0; x < size.x; ++x, ++index)
        {
          const glm::ivec3 sample = region.min + glm::ivec3(x, y, z);
          ASSERT_EQ(distance[index], terrain::decodeDistance(volume.distanceAtSample(sample)))
            << sample.x << "," << sample.y << "," << sample.z;
          ASSERT_EQ(material[index], volume.materialAtSample(sample))
            << sample.x << "," << sample.y << "," << sample.z;
        }
      }
    }
  }

  size_t sampleCount(const SampleBox& region)
  {
    const glm::ivec3 size = region.max - region.min;
    return static_cast<size_t>(size.x) * static_cast<size_t>(size.y) * static_cast<size_t>(size.z);
  }
}

TEST(TerrainSurfaceBoundsTest, EmptyBoxGivesEmptyBounds)
{
  EXPECT_TRUE(terrain::surfaceBounds({}).empty());
}

TEST(TerrainSurfaceBoundsTest, PadsByOneAirSampleThenRoundsOut)
{
  const SampleBox first = terrain::surfaceBounds({ glm::ivec3(0), glm::ivec3(32) });
  EXPECT_EQ(first.min, glm::ivec3(-64));
  EXPECT_EQ(first.max, glm::ivec3(64));

  const SampleBox negative = terrain::surfaceBounds({ glm::ivec3(-32), glm::ivec3(0) });
  EXPECT_EQ(negative.min, glm::ivec3(-64));
  EXPECT_EQ(negative.max, glm::ivec3(64));

  const SampleBox multiple = terrain::surfaceBounds({ glm::ivec3(0), glm::ivec3(64) });
  EXPECT_EQ(multiple.min, glm::ivec3(-64));
  EXPECT_EQ(multiple.max, glm::ivec3(128));
}

TEST(TerrainSurfaceBoundsTest, AlwaysContainsThePaddedOccupiedBox)
{
  const SampleBox boxes[] = {
    { glm::ivec3(0), glm::ivec3(32) },
    { glm::ivec3(-32), glm::ivec3(0) },
    { glm::ivec3(0), glm::ivec3(64) },
    { glm::ivec3(-96, 32, 5), glm::ivec3(-64, 160, 70) },
  };

  for (const SampleBox& box : boxes)
  {
    const SampleBox padded{ box.min - glm::ivec3(1), box.max + glm::ivec3(1) };
    EXPECT_TRUE(terrain::containsBox(terrain::surfaceBounds(box), padded));
  }
}

TEST(TerrainOpenSurfaceBoundsTest, InsetsXAndZByOneSampleAndRoundsYLikeAClosedSurface)
{
  const SampleBox occupied{ glm::ivec3(0), glm::ivec3(128, 64, 128) };
  const SampleBox open = terrain::openSurfaceBounds(occupied);

  EXPECT_EQ(open.min, glm::ivec3(1, -64, 1));
  EXPECT_EQ(open.max, glm::ivec3(127, 128, 127));

  // Positive control: a closed surface over the same box pads and rounds x and z outward instead.
  const SampleBox closed = terrain::surfaceBounds(occupied);
  EXPECT_EQ(closed.min, glm::ivec3(-64));
  EXPECT_EQ(open.min.y, closed.min.y);
  EXPECT_EQ(open.max.y, closed.max.y);
  EXPECT_NE(open.min.x, closed.min.x);
}

TEST(TerrainOpenSurfaceBoundsTest, InsetsWithoutRounding)
{
  const SampleBox open = terrain::openSurfaceBounds({ glm::ivec3(-32, 0, 5), glm::ivec3(70, 32, 91) });

  EXPECT_EQ(open.min.x, -31);
  EXPECT_EQ(open.max.x, 69);
  EXPECT_EQ(open.min.z, 6);
  EXPECT_EQ(open.max.z, 90);
}

TEST(TerrainOpenSurfaceBoundsTest, EmptyAndTooThinBoxesGiveEmptyBounds)
{
  EXPECT_TRUE(terrain::openSurfaceBounds({}).empty());

  // Two samples remain after the inset on a four-sample axis, which is a cell; one remains on a three-sample axis.
  EXPECT_FALSE(terrain::openSurfaceBounds({ glm::ivec3(0), glm::ivec3(4, 32, 4) }).empty());
  EXPECT_TRUE(terrain::openSurfaceBounds({ glm::ivec3(0), glm::ivec3(3, 32, 64) }).empty());
  EXPECT_TRUE(terrain::openSurfaceBounds({ glm::ivec3(0), glm::ivec3(64, 32, 3) }).empty());
}

TEST(TerrainOpenSurfaceBoundsTest, SampleBoundsGrowTheMeshedBoxOnXAndZOnly)
{
  const SampleBox meshed = terrain::openSurfaceBounds({ glm::ivec3(0), glm::ivec3(128, 64, 128) });
  const SampleBox samples = terrain::openSampleBounds(meshed);

  EXPECT_EQ(samples.min, glm::ivec3(0, meshed.min.y, 0));
  EXPECT_EQ(samples.max, glm::ivec3(128, meshed.max.y, 128));
  EXPECT_TRUE(terrain::containsBox(samples, meshed));
  EXPECT_TRUE(terrain::openSampleBounds({}).empty());
}

TEST(TerrainOpenSurfaceBoundsTest, DesiredBoundsFollowTheOpenSidesFlag)
{
  const SampleBox occupied{ glm::ivec3(0), glm::ivec3(128, 64, 128) };
  const SampleBox open = terrain::desiredSurfaceBounds(occupied, true);
  const SampleBox closed = terrain::desiredSurfaceBounds(occupied, false);

  EXPECT_EQ(open.min, terrain::openSurfaceBounds(occupied).min);
  EXPECT_EQ(closed.min, terrain::surfaceBounds(occupied).min);
  EXPECT_NE(open.min, closed.min);
}

TEST(TerrainContainsBoxTest, InsideTouchingOutsideAndEmpty)
{
  const SampleBox outer{ glm::ivec3(-10), glm::ivec3(10) };

  EXPECT_TRUE(terrain::containsBox(outer, { glm::ivec3(-5), glm::ivec3(5) }));
  EXPECT_TRUE(terrain::containsBox(outer, outer));
  EXPECT_FALSE(terrain::containsBox(outer, { glm::ivec3(-5), glm::ivec3(5, 5, 11) }));
  EXPECT_FALSE(terrain::containsBox(outer, { glm::ivec3(-11, -5, -5), glm::ivec3(5) }));
  EXPECT_TRUE(terrain::containsBox(outer, { glm::ivec3(100), glm::ivec3(100) }));
}

TEST(TerrainSurfaceChunkCountTest, CountsCellsPerAxis)
{
  EXPECT_EQ(terrain::surfaceChunkCount({ glm::ivec3(-64), glm::ivec3(64) }), 64u);
  EXPECT_EQ(terrain::surfaceChunkCount({ glm::ivec3(0), glm::ivec3(33) }), 1u);
  EXPECT_EQ(terrain::surfaceChunkCount({ glm::ivec3(0), glm::ivec3(34) }), 8u);
  EXPECT_EQ(terrain::surfaceChunkCount({ glm::ivec3(0), glm::ivec3(1, 64, 64) }), 0u);
}

TEST(TerrainSurfaceChunkCountTest, HugeBoxDoesNotOverflow)
{
  const SampleBox huge{ glm::ivec3(INT32_MIN), glm::ivec3(INT32_MAX) };
  EXPECT_EQ(terrain::surfaceChunkCount(huge), UINT64_MAX);
  EXPECT_GT(terrain::surfaceChunkCount(huge), terrain::maxSurfaceChunks);

  const SampleBox wide{ glm::ivec3(0), glm::ivec3(1 << 30, 33, 33) };
  EXPECT_EQ(terrain::surfaceChunkCount(wide), uint64_t{ 1 << 25 });
}

TEST(TerrainChangedChunksTest, IdenticalMapsReportNothingAndReplacementIsReported)
{
  terrain::TerrainVolume::ChunkMap before;
  before[{ 0, 0, 0 }] = solid(1);
  before[{ 1, 0, 0 }] = solid(2);

  auto after = before;
  EXPECT_TRUE(terrain::changedChunks(before, after).empty());

  after[{ 1, 0, 0 }] = solid(2);
  const auto changed = terrain::changedChunks(before, after);
  ASSERT_EQ(changed.size(), 1u);
  EXPECT_EQ(changed[0], (ChunkCoord{ 1, 0, 0 }));
}

TEST(TerrainChangedChunksTest, AddedRemovedAndReplacedAreSorted)
{
  terrain::TerrainVolume::ChunkMap before;
  before[{ 0, 0, 0 }] = solid(1);
  before[{ 5, 0, 0 }] = solid(1);
  before[{ 2, 0, 0 }] = solid(1);

  auto after = before;
  after.erase({ 5, 0, 0 });
  after[{ 2, 0, 0 }] = solid(3);
  after[{ 9, -1, 0 }] = solid(1);
  after[{ -3, 0, -2 }] = solid(1);

  const auto changed = terrain::changedChunks(before, after);
  const std::vector<ChunkCoord> expected{ { -3, 0, -2 }, { 9, -1, 0 }, { 2, 0, 0 }, { 5, 0, 0 } };
  EXPECT_EQ(changed, expected);
}

TEST(TerrainChunkSampleBoundsTest, CoversEveryChunk)
{
  EXPECT_TRUE(terrain::chunkSampleBounds({}).empty());

  const std::vector<ChunkCoord> chunks{ { 0, 0, 0 }, { -1, 2, 0 } };
  const SampleBox bounds = terrain::chunkSampleBounds(chunks);
  EXPECT_EQ(bounds.min, glm::ivec3(-32, 0, 0));
  EXPECT_EQ(bounds.max, glm::ivec3(32, 96, 32));
}

TEST(TerrainFillSurfaceSamplesTest, MatchesTheVolumeAcrossChunkBorders)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ 0, 0, 0 }, patterned());
  volume.setChunk({ -1, 0, 0 }, solid(7));
  volume.setChunk({ 0, -1, 0 }, patterned());

  const SampleBox region{ glm::ivec3(-3), glm::ivec3(4, 5, 6) };
  std::vector<float> distance(sampleCount(region), -123.0f);
  std::vector<uint8_t> material(sampleCount(region), 99);

  ASSERT_TRUE(terrain::fillSurfaceSamples(volume.chunks(), region, 0, distance, material));
  expectMatchesVolume(volume, region, distance, material);

  // Positive control: the region holds absent, solid and patterned samples.
  EXPECT_NE(std::ranges::find(distance, 4.0f), distance.end());
  EXPECT_NE(std::ranges::find(distance, -4.0f), distance.end());
  EXPECT_NE(std::ranges::find_if(distance, [](const float value) { return value != 4.0f && value != -4.0f; }),
            distance.end());
}

TEST(TerrainFillSurfaceSamplesTest, RegionInsideOneDenseChunkAtANonZeroOffset)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ 1, -1, 0 }, patterned());

  const SampleBox region{ glm::ivec3(40, -20, 5), glm::ivec3(47, -15, 9) };
  std::vector<float> distance(sampleCount(region), -123.0f);
  std::vector<uint8_t> material(sampleCount(region), 99);

  ASSERT_TRUE(terrain::fillSurfaceSamples(volume.chunks(), region, 0, distance, material));
  expectMatchesVolume(volume, region, distance, material);
  EXPECT_NE(std::ranges::find_if(distance, [](const float value) { return value != 4.0f; }), distance.end());
}

TEST(TerrainFillSurfaceSamplesTest, RegionSpanningManyChunksOnOneAxis)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ -1, 0, 0 }, patterned());
  volume.setChunk({ 0, 0, 0 }, solid(4));
  volume.setChunk({ 2, 0, 0 }, patterned());

  const SampleBox region{ glm::ivec3(-5, 0, 0), glm::ivec3(70, 2, 2) };
  std::vector<float> distance(sampleCount(region), -123.0f);
  std::vector<uint8_t> material(sampleCount(region), 99);

  ASSERT_TRUE(terrain::fillSurfaceSamples(volume.chunks(), region, 0, distance, material));
  expectMatchesVolume(volume, region, distance, material);
  EXPECT_NE(std::ranges::find(distance, 4.0f), distance.end());
  EXPECT_NE(std::ranges::find(distance, -4.0f), distance.end());
}

TEST(TerrainFillSurfaceSamplesTest, LodOneOverAnAbsentChunkReadsAir)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ 0, 0, 0 }, solid(5));

  const SampleBox region{ glm::ivec3(20, 0, 0), glm::ivec3(24, 2, 2) };
  std::vector<float> distance(sampleCount(region), 0.0f);
  std::vector<uint8_t> material(sampleCount(region), 99);

  ASSERT_TRUE(terrain::fillSurfaceSamples(volume.chunks(), region, 1, distance, material));

  // Offsets 0..3 read samples 40, 42, 44 and 46, all in the absent chunk (1,0,0).
  for (const float value : distance)
  {
    EXPECT_EQ(value, 4.0f);
  }

  const SampleBox mixed{ glm::ivec3(14, 0, 0), glm::ivec3(18, 1, 1) };
  std::vector<float> mixedDistance(sampleCount(mixed), 0.0f);
  std::vector<uint8_t> mixedMaterial(sampleCount(mixed), 99);
  ASSERT_TRUE(terrain::fillSurfaceSamples(volume.chunks(), mixed, 1, mixedDistance, mixedMaterial));
  EXPECT_EQ(mixedDistance[0], -4.0f);
  EXPECT_EQ(mixedMaterial[0], 5);
  EXPECT_EQ(mixedDistance[3], 4.0f);
  EXPECT_EQ(mixedMaterial[3], 0);
}

TEST(TerrainFillSurfaceSamplesTest, AbsentChunkReadsAsAir)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ 0, 0, 0 }, solid(5));

  const SampleBox region{ glm::ivec3(40, 0, 0), glm::ivec3(44, 2, 2) };
  std::vector<float> distance(sampleCount(region), 0.0f);
  std::vector<uint8_t> material(sampleCount(region), 99);

  ASSERT_TRUE(terrain::fillSurfaceSamples(volume.chunks(), region, 0, distance, material));

  for (const float value : distance)
  {
    EXPECT_EQ(value, 4.0f);
  }

  for (const uint8_t value : material)
  {
    EXPECT_EQ(value, 0);
  }

  const SampleBox held{ glm::ivec3(0), glm::ivec3(2) };
  std::vector<float> heldDistance(sampleCount(held));
  std::vector<uint8_t> heldMaterial(sampleCount(held));
  ASSERT_TRUE(terrain::fillSurfaceSamples(volume.chunks(), held, 0, heldDistance, heldMaterial));
  EXPECT_EQ(heldDistance[0], -4.0f);
  EXPECT_EQ(heldMaterial[0], 5);
}

TEST(TerrainFillSurfaceSamplesTest, StoredDistanceIsDecodedToVoxels)
{
  const int16_t stored = terrain::encodeDistance(-1.5f);
  terrain::TerrainVolume volume;
  volume.setChunk({ 0, 0, 0 }, terrain::TerrainChunk::uniform(stored, 3));

  std::vector<float> distance(1);
  std::vector<uint8_t> material(1);
  ASSERT_TRUE(terrain::fillSurfaceSamples(volume.chunks(), { glm::ivec3(1), glm::ivec3(2) }, 0, distance, material));

  EXPECT_EQ(distance[0], terrain::decodeDistance(stored));
  EXPECT_NEAR(distance[0], -1.5f, 1e-3f);
  EXPECT_EQ(material[0], 3);
}

TEST(TerrainFillSurfaceSamplesTest, LodOneReadsEverySecondSample)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ 0, 0, 0 }, patterned());
  volume.setChunk({ -1, 0, 0 }, patterned());

  const SampleBox region{ glm::ivec3(-3, 0, 0), glm::ivec3(4, 3, 3) };
  std::vector<float> distance(sampleCount(region));
  std::vector<uint8_t> material(sampleCount(region));
  ASSERT_TRUE(terrain::fillSurfaceSamples(volume.chunks(), region, 1, distance, material));

  const glm::ivec3 size = region.max - region.min;
  size_t index = 0;

  for (int z = 0; z < size.z; ++z)
  {
    for (int y = 0; y < size.y; ++y)
    {
      for (int x = 0; x < size.x; ++x, ++index)
      {
        const glm::ivec3 sample = (region.min + glm::ivec3(x, y, z)) * 2;
        ASSERT_EQ(distance[index], terrain::decodeDistance(volume.distanceAtSample(sample)));
        ASSERT_EQ(material[index], volume.materialAtSample(sample));
      }
    }
  }
}

TEST(TerrainFillSurfaceSamplesTest, SnapshotIsIsolatedFromLaterEdits)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ 0, 0, 0 }, solid(1));
  const auto snapshot = volume.snapshotChunks();
  volume.setChunk({ 0, 0, 0 }, solid(9));

  const SampleBox region{ glm::ivec3(0), glm::ivec3(2) };
  std::vector<float> distance(sampleCount(region));
  std::vector<uint8_t> oldMaterial(sampleCount(region));
  std::vector<uint8_t> newMaterial(sampleCount(region));

  ASSERT_TRUE(terrain::fillSurfaceSamples(snapshot, region, 0, distance, oldMaterial));
  ASSERT_TRUE(terrain::fillSurfaceSamples(volume.chunks(), region, 0, distance, newMaterial));

  EXPECT_EQ(oldMaterial[0], 1);
  EXPECT_EQ(newMaterial[0], 9);
}

TEST(TerrainFillSurfaceSamplesTest, RefusesBadArgumentsWithoutWriting)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ 0, 0, 0 }, solid(1));

  const SampleBox region{ glm::ivec3(0), glm::ivec3(2) };
  std::vector<float> distance(8, -77.0f);
  std::vector<uint8_t> material(8, 77);
  std::vector<float> shortDistance(7, -77.0f);
  std::vector<uint8_t> shortMaterial(7, 77);

  EXPECT_FALSE(terrain::fillSurfaceSamples(volume.chunks(), region, 0, shortDistance, material));
  EXPECT_FALSE(terrain::fillSurfaceSamples(volume.chunks(), region, 0, distance, shortMaterial));
  EXPECT_FALSE(terrain::fillSurfaceSamples(volume.chunks(), {}, 0, distance, material));
  EXPECT_FALSE(terrain::fillSurfaceSamples(volume.chunks(), region, 9, distance, material));

  for (const float value : distance)
  {
    EXPECT_EQ(value, -77.0f);
  }

  for (const uint8_t value : material)
  {
    EXPECT_EQ(value, 77);
  }

  EXPECT_TRUE(terrain::fillSurfaceSamples(volume.chunks(), region, 0, distance, material));
  EXPECT_EQ(material[0], 1);
}

namespace {
  SampleBox box(const int minimum, const int maximum)
  {
    return { glm::ivec3(minimum), glm::ivec3(maximum) };
  }

  const SampleBox smallBox = box(0, 64);
  const SampleBox largeBox = box(0, 128);
}

TEST(TerrainSurfacePlanTest, NothingHeldCreatesTheCurrentSurfaceWithTheDesiredBounds)
{
  const auto plan = terrain::planTerrainSurface(smallBox, {});

  EXPECT_EQ(plan.step, terrain::SurfaceStep::createCurrent);
  EXPECT_EQ(plan.bounds.min, smallBox.min);
  EXPECT_EQ(plan.bounds.max, smallBox.max);
}

TEST(TerrainSurfacePlanTest, ACurrentSurfaceThatContainsTheDesiredBoundsKeeps)
{
  terrain::SurfaceSlots slots;
  slots.current = largeBox;

  EXPECT_EQ(terrain::planTerrainSurface(smallBox, slots).step, terrain::SurfaceStep::keep);
  EXPECT_EQ(terrain::planTerrainSurface(largeBox, slots).step, terrain::SurfaceStep::keep);
  EXPECT_EQ(terrain::planTerrainSurface(box(0, 129), slots).step, terrain::SurfaceStep::createReplacement);
}

TEST(TerrainSurfacePlanTest, AnOutgrownCurrentSurfaceBuildsAReplacementWithTheDesiredBounds)
{
  terrain::SurfaceSlots slots;
  slots.current = smallBox;

  const auto plan = terrain::planTerrainSurface(largeBox, slots);

  EXPECT_EQ(plan.step, terrain::SurfaceStep::createReplacement);
  EXPECT_EQ(plan.bounds.max, largeBox.max);
}

TEST(TerrainSurfacePlanTest, NothingNewIsCreatedWhileAReplacementBuilds)
{
  terrain::SurfaceSlots slots;
  slots.current = smallBox;
  slots.replacement = largeBox;

  EXPECT_EQ(terrain::planTerrainSurface(box(0, 256), slots).step, terrain::SurfaceStep::keep);

  slots.replacement.reset();
  EXPECT_EQ(terrain::planTerrainSurface(box(0, 256), slots).step, terrain::SurfaceStep::createReplacement);
}

TEST(TerrainSurfacePlanTest, AFinishedReplacementIsPromotedEvenWhenTheTerrainGrewAgain)
{
  terrain::SurfaceSlots slots;
  slots.current = smallBox;
  slots.replacement = largeBox;
  slots.replacementPending = 0.0f;

  const auto plan = terrain::planTerrainSurface(box(0, 256), slots);

  EXPECT_EQ(plan.step, terrain::SurfaceStep::promote);
  EXPECT_EQ(plan.bounds.max, largeBox.max);
  EXPECT_EQ(terrain::planTerrainSurface({}, slots).step, terrain::SurfaceStep::promote);
}

TEST(TerrainSurfacePlanTest, AReplacementNoLessCompleteThanTheDrawnSurfaceIsPromoted)
{
  terrain::SurfaceSlots slots;
  slots.current = smallBox;
  slots.replacement = largeBox;

  slots.currentPending = 0.4f;
  slots.replacementPending = 0.4f;
  EXPECT_EQ(terrain::planTerrainSurface(largeBox, slots).step, terrain::SurfaceStep::promote);

  slots.replacementPending = 0.1f;
  EXPECT_EQ(terrain::planTerrainSurface(largeBox, slots).step, terrain::SurfaceStep::promote);

  slots.replacementPending = 0.5f;
  EXPECT_EQ(terrain::planTerrainSurface(largeBox, slots).step, terrain::SurfaceStep::keep);
}

TEST(TerrainSurfacePlanTest, AnEmptyDesiredBoxKeepsWhateverIsHeld)
{
  EXPECT_EQ(terrain::planTerrainSurface({}, {}).step, terrain::SurfaceStep::keep);

  terrain::SurfaceSlots slots;
  slots.current = smallBox;

  EXPECT_EQ(terrain::planTerrainSurface({}, slots).step, terrain::SurfaceStep::keep);
  EXPECT_EQ(terrain::planTerrainSurface(largeBox, slots).step, terrain::SurfaceStep::createReplacement);
}

TEST(TerrainSurfacePlanTest, TooManyChunksCreateNothingOnEitherCreatePath)
{
  // 1025 cells on an axis is 33 surface chunks; 1024 is 32, so the first box is exactly the cap.
  const SampleBox atCap = box(0, 1025);
  const SampleBox overCap{ glm::ivec3(0), glm::ivec3(1026, 1025, 1025) };

  ASSERT_EQ(terrain::surfaceChunkCount(atCap), terrain::maxSurfaceChunks);
  ASSERT_GT(terrain::surfaceChunkCount(overCap), terrain::maxSurfaceChunks);

  EXPECT_EQ(terrain::planTerrainSurface(atCap, {}).step, terrain::SurfaceStep::createCurrent);
  EXPECT_EQ(terrain::planTerrainSurface(overCap, {}).step, terrain::SurfaceStep::tooLarge);

  terrain::SurfaceSlots slots;
  slots.current = smallBox;

  EXPECT_EQ(terrain::planTerrainSurface(atCap, slots).step, terrain::SurfaceStep::createReplacement);
  EXPECT_EQ(terrain::planTerrainSurface(overCap, slots).step, terrain::SurfaceStep::tooLarge);
}

TEST(TerrainSurfacePlanTest, AnExactMatchReplacesALargerSurfaceTheContainsRuleKeeps)
{
  terrain::SurfaceSlots slots;
  slots.current = largeBox;

  // Positive control: without exact matching the larger drawn surface is kept.
  ASSERT_EQ(terrain::planTerrainSurface(smallBox, slots).step, terrain::SurfaceStep::keep);

  const auto plan = terrain::planTerrainSurface(smallBox, slots, true);
  EXPECT_EQ(plan.step, terrain::SurfaceStep::createReplacement);
  EXPECT_EQ(plan.bounds.max, smallBox.max);

  EXPECT_EQ(terrain::planTerrainSurface(largeBox, slots, true).step, terrain::SurfaceStep::keep);
  EXPECT_EQ(terrain::planTerrainSurface(smallBox, {}, true).step, terrain::SurfaceStep::createCurrent);
}

TEST(TerrainSurfacePlanTest, ModelMatrixTranslatesAndRotates)
{
  const glm::quat quarterTurn = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
  const glm::mat4 model = terrain::terrainModelMatrix(glm::vec3(10.0f, 20.0f, 30.0f), quarterTurn);

  const glm::vec3 mapped = glm::vec3(model * glm::vec4(2.0f, 0.0f, 0.0f, 1.0f));

  EXPECT_NEAR(mapped.x, 10.0f, 1e-4f);
  EXPECT_NEAR(mapped.y, 22.0f, 1e-4f);
  EXPECT_NEAR(mapped.z, 30.0f, 1e-4f);
}

TEST(TerrainSurfaceFeedTest, SnapshotReturnsTheConstructorsMap)
{
  const auto first = std::make_shared<const terrain::TerrainVolume::ChunkMap>();
  const TerrainSurfaceFeed feed(first);

  EXPECT_EQ(feed.snapshot(), first);
}

TEST(TerrainSurfaceFeedTest, PublishSwapsTheSnapshotAndReportsOnce)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ 0, 0, 0 }, solid());

  const auto first = std::make_shared<const terrain::TerrainVolume::ChunkMap>();
  const auto second = std::make_shared<const terrain::TerrainVolume::ChunkMap>(volume.snapshotChunks());
  TerrainSurfaceFeed feed(first);

  int reports = 0;
  feed.publish(second, [&reports] { ++reports; });

  EXPECT_EQ(reports, 1);
  EXPECT_EQ(feed.snapshot(), second);
  EXPECT_NE(feed.snapshot(), first);
  EXPECT_EQ(feed.snapshot()->size(), 1u);
}

TEST(TerrainSurfaceFeedTest, PublishAcceptsAnEmptyReport)
{
  const auto first = std::make_shared<const terrain::TerrainVolume::ChunkMap>();
  const auto second = std::make_shared<const terrain::TerrainVolume::ChunkMap>();
  TerrainSurfaceFeed feed(first);

  feed.publish(second, {});

  EXPECT_EQ(feed.snapshot(), second);
}

TEST(TerrainVolumeSourceTest, ReportsTheBoundsAndTheSampleBoundsItWasGiven)
{
  const auto feed = std::make_shared<const TerrainSurfaceFeed>(std::make_shared<const terrain::TerrainVolume::ChunkMap>());
  const SampleBox meshed{ glm::ivec3(1, -64, 1), glm::ivec3(127, 128, 127) };
  const SampleBox samples{ glm::ivec3(0, -64, 0), glm::ivec3(128, 128, 128) };

  const TerrainVolumeSource open(feed, meshed, samples);
  EXPECT_EQ(open.bounds().min, meshed.min);
  EXPECT_EQ(open.bounds().max, meshed.max);
  EXPECT_EQ(open.sampleBounds().min, samples.min);
  EXPECT_EQ(open.sampleBounds().max, samples.max);

  // Positive control: without sample bounds they are the bounds.
  const TerrainVolumeSource closed(feed, meshed);
  EXPECT_EQ(closed.sampleBounds().min, meshed.min);
  EXPECT_EQ(closed.sampleBounds().max, meshed.max);
}

TEST(TerrainVolumeSourceTest, AnswersSamplesOutsideItsBoundsFromTheFeed)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ 0, 0, 0 }, solid());

  const auto feed = std::make_shared<const TerrainSurfaceFeed>(
    std::make_shared<const terrain::TerrainVolume::ChunkMap>(volume.snapshotChunks()));
  const TerrainVolumeSource source(feed, { glm::ivec3(1), glm::ivec3(8) }, { glm::ivec3(0), glm::ivec3(9) });

  std::vector<float> distance(2);
  std::vector<uint8_t> material(2);
  source.fill({ glm::ivec3(-1, 0, 0), glm::ivec3(1, 1, 1) }, 0, distance, material);

  EXPECT_GT(distance[0], 0.0f);
  EXPECT_LT(distance[1], 0.0f);
}
