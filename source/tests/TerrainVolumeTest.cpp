#include <gtest/gtest.h>

#include "terrain/TerrainChunk.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <glm/vec3.hpp>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
  std::shared_ptr<const terrain::TerrainChunk> solid(const uint8_t material = 1)
  {
    return terrain::TerrainChunk::uniform(-terrain::airDistance, material);
  }

  std::shared_ptr<const terrain::TerrainChunk> ramp(const int16_t base)
  {
    std::vector<int16_t> distances(terrain::samplesPerChunk);
    std::vector<uint8_t> materials(terrain::samplesPerChunk, 2);

    for (int i = 0; i < terrain::samplesPerChunk; ++i)
    {
      distances[static_cast<size_t>(i)] = static_cast<int16_t>(base + (i % 100));
    }

    return terrain::TerrainChunk::dense(std::move(distances), std::move(materials));
  }

  terrain::TerrainVolume makeVolume(const float voxelSize)
  {
    return terrain::TerrainVolume(voxelSize);
  }
}

TEST(TerrainVolumeTest, AbsentChunkReadsAsAir)
{
  const terrain::TerrainVolume volume;

  EXPECT_EQ(volume.chunk({ 0, 0, 0 }), nullptr);
  EXPECT_EQ(volume.distanceAtSample({ 5, -7, 100 }), terrain::airDistance);
  EXPECT_EQ(volume.materialAtSample({ 5, -7, 100 }), 0);
  EXPECT_TRUE(volume.chunks().empty());
}

TEST(TerrainVolumeTest, SetChunkRoundTrips)
{
  terrain::TerrainVolume volume;
  const auto chunk = solid(4);

  volume.setChunk({ 1, -2, 3 }, chunk);

  EXPECT_EQ(volume.chunk({ 1, -2, 3 }), chunk);
  EXPECT_EQ(volume.chunk({ 1, -2, 4 }), nullptr);
  EXPECT_EQ(volume.chunks().size(), 1u);
  EXPECT_EQ(volume.distanceAtSample({ 32, -64, 96 }), -terrain::airDistance);
  EXPECT_EQ(volume.materialAtSample({ 32, -64, 96 }), 4);
}

TEST(TerrainVolumeTest, NullAndAirChunksEraseTheEntry)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ 0, 0, 0 }, solid());
  volume.setChunk({ 1, 0, 0 }, solid());
  ASSERT_EQ(volume.chunks().size(), 2u);

  volume.setChunk({ 0, 0, 0 }, nullptr);
  volume.setChunk({ 1, 0, 0 }, terrain::TerrainChunk::uniform(terrain::airDistance, 3));

  EXPECT_TRUE(volume.chunks().empty());
  EXPECT_EQ(volume.chunk({ 0, 0, 0 }), nullptr);

  volume.setChunk({ 2, 0, 0 }, terrain::TerrainChunk::uniform(terrain::airDistance, 0));
  EXPECT_TRUE(volume.chunks().empty());

  volume.setChunk({ 2, 0, 0 }, solid());
  EXPECT_EQ(volume.chunks().size(), 1u);
}

TEST(TerrainVolumeTest, CopyOnWriteLeavesHeldChunksUntouched)
{
  terrain::TerrainVolume volume;
  const auto original = ramp(-1000);
  volume.setChunk({ 0, 0, 0 }, original);

  const auto held = volume.chunk({ 0, 0, 0 });
  const auto snapshot = volume.snapshotChunks();
  const int16_t before = held->distanceAt(7, 0, 0);

  volume.setChunk({ 0, 0, 0 }, ramp(5000));

  EXPECT_EQ(held, original);
  EXPECT_EQ(held->distanceAt(7, 0, 0), before);
  EXPECT_EQ(snapshot.at({ 0, 0, 0 }), original);
  EXPECT_NE(volume.chunk({ 0, 0, 0 }), original);
  EXPECT_EQ(volume.distanceAtSample({ 7, 0, 0 }), 5007);
}

TEST(TerrainVolumeTest, RestoreChunksBringsBackTheOldState)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ 0, 0, 0 }, solid(1));
  volume.setChunk({ -1, 0, 0 }, solid(2));
  const auto snapshot = volume.snapshotChunks();

  volume.setChunk({ 0, 0, 0 }, ramp(0));
  volume.setChunk({ -1, 0, 0 }, nullptr);
  volume.setChunk({ 5, 5, 5 }, solid(3));

  volume.restoreChunks(snapshot);

  EXPECT_EQ(volume.chunks().size(), 2u);
  EXPECT_EQ(volume.chunk({ 0, 0, 0 }), snapshot.at({ 0, 0, 0 }));
  EXPECT_EQ(volume.materialAtSample({ -1, 0, 0 }), 2);
  EXPECT_EQ(volume.chunk({ 5, 5, 5 }), nullptr);
}

TEST(TerrainVolumeTest, RestoreChunksDropsAirAndNullEntries)
{
  terrain::TerrainVolume volume;
  terrain::TerrainVolume::ChunkMap map;
  map[{ 0, 0, 0 }] = nullptr;
  map[{ 1, 0, 0 }] = terrain::TerrainChunk::uniform(terrain::airDistance, 0);
  map[{ 2, 0, 0 }] = solid();

  volume.restoreChunks(map);

  EXPECT_EQ(volume.chunks().size(), 1u);
  EXPECT_NE(volume.chunk({ 2, 0, 0 }), nullptr);
}

TEST(TerrainVolumeTest, DistanceReadsAcrossAChunkBorderWithNegativeCoordinates)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ -1, -1, -1 }, terrain::TerrainChunk::uniform(-100, 5));
  volume.setChunk({ 0, -1, -1 }, terrain::TerrainChunk::uniform(300, 6));

  EXPECT_EQ(volume.distanceAtSample({ -1, -1, -1 }), -100);
  EXPECT_EQ(volume.materialAtSample({ -1, -1, -1 }), 5);
  EXPECT_EQ(volume.distanceAtSample({ 0, -1, -1 }), 300);
  EXPECT_EQ(volume.materialAtSample({ 0, -1, -1 }), 6);
  EXPECT_EQ(volume.distanceAtSample({ -33, -1, -1 }), terrain::airDistance);
  EXPECT_EQ(volume.distanceAtSample({ -32, -32, -32 }), -100);
}

TEST(TerrainVolumeTest, DenseChunkSampleLandsAtItsLocalIndex)
{
  std::vector<int16_t> distances(terrain::samplesPerChunk, 0);
  std::vector<uint8_t> materials(terrain::samplesPerChunk, 0);
  distances[static_cast<size_t>(terrain::sampleIndex(31, 0, 31))] = 123;
  materials[static_cast<size_t>(terrain::sampleIndex(31, 0, 31))] = 9;

  terrain::TerrainVolume volume;
  volume.setChunk({ -1, 0, -1 }, terrain::TerrainChunk::dense(std::move(distances), std::move(materials)));

  EXPECT_EQ(volume.distanceAtSample({ -1, 0, -1 }), 123);
  EXPECT_EQ(volume.materialAtSample({ -1, 0, -1 }), 9);
  EXPECT_EQ(volume.distanceAtSample({ -2, 0, -1 }), 0);
}

TEST(TerrainVolumeTest, OccupiedBoundsCoversEveryHeldChunk)
{
  terrain::TerrainVolume volume;
  EXPECT_TRUE(volume.occupiedBounds().empty());

  volume.setChunk({ -2, 0, 1 }, solid());
  volume.setChunk({ 1, -1, 1 }, solid());

  const terrain::SampleBox bounds = volume.occupiedBounds();

  EXPECT_FALSE(bounds.empty());
  EXPECT_EQ(bounds.min, glm::ivec3(-64, -32, 32));
  EXPECT_EQ(bounds.max, glm::ivec3(64, 32, 64));

  volume.setChunk({ -2, 0, 1 }, nullptr);
  volume.setChunk({ 1, -1, 1 }, nullptr);
  EXPECT_TRUE(volume.occupiedBounds().empty());
}

TEST(TerrainVolumeTest, ByteSizeGrowsWithDenseChunks)
{
  terrain::TerrainVolume volume;
  const size_t empty = volume.byteSize();

  volume.setChunk({ 0, 0, 0 }, solid());
  const size_t withUniform = volume.byteSize();
  volume.setChunk({ 1, 0, 0 }, ramp(0));

  EXPECT_GT(withUniform, empty);
  EXPECT_GT(volume.byteSize(), withUniform + static_cast<size_t>(terrain::samplesPerChunk) * 3);
}

TEST(TerrainVolumeTest, ExtremeChunkCoordinatesThrowAndLeaveTheVolumeUnchanged)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ 0, 0, 0 }, solid(1));
  const auto before = volume.snapshotChunks();
  constexpr int32_t beyond = terrain::maxChunkCoord + 1;
  constexpr int32_t minimum = std::numeric_limits<int32_t>::min();

  EXPECT_THROW(volume.setChunk({ beyond, 0, 0 }, solid()), std::out_of_range);
  EXPECT_THROW(volume.setChunk({ 0, -beyond, 0 }, solid()), std::out_of_range);
  EXPECT_THROW(volume.setChunk({ 0, 0, std::numeric_limits<int32_t>::max() }, solid()), std::out_of_range);
  EXPECT_THROW(volume.setChunk({ minimum, 0, 0 }, solid()), std::out_of_range);
  EXPECT_THROW(volume.setChunk({ beyond, 0, 0 }, nullptr), std::out_of_range);

  terrain::TerrainVolume::ChunkMap bad;
  bad[{ 5, 5, 5 }] = solid(2);
  bad[{ 0, minimum, 0 }] = solid(3);
  EXPECT_THROW(volume.restoreChunks(bad), std::out_of_range);

  EXPECT_EQ(volume.chunks().size(), 1u);
  EXPECT_EQ(volume.chunk({ 0, 0, 0 }), before.at({ 0, 0, 0 }));
  EXPECT_EQ(volume.chunk({ 5, 5, 5 }), nullptr);
}

TEST(TerrainVolumeTest, AChunkAtExactlyTheCoordinateLimitIsAccepted)
{
  terrain::TerrainVolume volume;
  constexpr int32_t limit = terrain::maxChunkCoord;

  EXPECT_TRUE(terrain::isValidChunkCoord({ limit, -limit, limit }));
  EXPECT_FALSE(terrain::isValidChunkCoord({ limit + 1, 0, 0 }));

  volume.setChunk({ limit, -limit, 0 }, solid(1));

  EXPECT_EQ(volume.chunks().size(), 1u);
  const terrain::SampleBox bounds = volume.occupiedBounds();
  EXPECT_EQ(bounds.min, glm::ivec3(limit * 32, -limit * 32, 0));
  EXPECT_EQ(bounds.max, glm::ivec3(limit * 32 + 32, -limit * 32 + 32, 32));
  EXPECT_EQ(volume.distanceAtSample({ limit * 32, -limit * 32, 0 }), -terrain::airDistance);

  terrain::TerrainVolume::ChunkMap map;
  map[{ -limit, limit, limit }] = solid(2);
  volume.restoreChunks(map);
  EXPECT_EQ(volume.chunks().size(), 1u);
}

TEST(TerrainVolumeTest, VoxelSizeIsKeptAndInvalidSizesThrow)
{
  EXPECT_FLOAT_EQ(terrain::TerrainVolume().voxelSize(), 2.5f);
  EXPECT_FLOAT_EQ(terrain::TerrainVolume(0.5f).voxelSize(), 0.5f);

  EXPECT_THROW(makeVolume(0.0f), std::invalid_argument);
  EXPECT_THROW(makeVolume(-1.0f), std::invalid_argument);
  EXPECT_THROW(makeVolume(std::numeric_limits<float>::quiet_NaN()), std::invalid_argument);
  EXPECT_THROW(makeVolume(std::numeric_limits<float>::infinity()), std::invalid_argument);
}

TEST(TerrainVolumeTest, RevisionStartsAtZeroAndBumpsOnInsertReplaceAndErase)
{
  terrain::TerrainVolume volume;
  EXPECT_EQ(volume.revision(), 0u);

  volume.setChunk({ 0, 0, 0 }, solid(1));
  EXPECT_EQ(volume.revision(), 1u);

  volume.setChunk({ 0, 0, 0 }, solid(2));
  EXPECT_EQ(volume.revision(), 2u);

  volume.setChunk({ 0, 0, 0 }, nullptr);
  EXPECT_EQ(volume.revision(), 3u);
}

TEST(TerrainVolumeTest, RevisionIgnoresNoOps)
{
  terrain::TerrainVolume volume;
  const auto held = solid(1);
  volume.setChunk({ 1, 0, 0 }, held);
  const uint64_t revision = volume.revision();

  volume.setChunk({ 1, 0, 0 }, held);
  volume.setChunk({ 5, 5, 5 }, nullptr);
  volume.setChunk({ 5, 5, 5 }, terrain::TerrainChunk::uniform(terrain::airDistance, 0));
  EXPECT_EQ(volume.revision(), revision);

  volume.setChunk({ 1, 0, 0 }, solid(1));
  EXPECT_EQ(volume.revision(), revision + 1);
}

TEST(TerrainVolumeTest, RevisionIsUnchangedByThrowingCalls)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ 0, 0, 0 }, solid(1));
  const uint64_t revision = volume.revision();

  const terrain::ChunkCoord outside{ terrain::maxChunkCoord + 1, 0, 0 };
  EXPECT_THROW(volume.setChunk(outside, solid(1)), std::out_of_range);

  terrain::TerrainVolume::ChunkMap map;
  map[outside] = solid(1);
  EXPECT_THROW(volume.restoreChunks(map), std::out_of_range);

  EXPECT_EQ(volume.revision(), revision);
}

TEST(TerrainVolumeTest, RestoreChunksBumpsTheRevision)
{
  terrain::TerrainVolume volume;
  const uint64_t revision = volume.revision();

  terrain::TerrainVolume::ChunkMap map;
  map[{ 0, 0, 0 }] = solid(1);
  volume.restoreChunks(map);
  EXPECT_EQ(volume.revision(), revision + 1);

  volume.restoreChunks(volume.snapshotChunks());
  EXPECT_EQ(volume.revision(), revision + 2);
}
