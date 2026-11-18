#include <gtest/gtest.h>

#include "TerrainSurfaceSampling.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <glm/vec3.hpp>
#include <algorithm>
#include <cstdint>
#include <memory>
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
