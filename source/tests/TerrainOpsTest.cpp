#include <gtest/gtest.h>

#include "terrain/TerrainChunk.h"
#include "terrain/TerrainOps.h"
#include "terrain/TerrainParallel.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <glm/vec3.hpp>
#include <atomic>
#include <cstdint>
#include <memory>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace {
  std::shared_ptr<const terrain::TerrainChunk> solid(const uint8_t material = 1)
  {
    return terrain::TerrainChunk::uniform(-terrain::airDistance, material);
  }

  std::shared_ptr<const terrain::TerrainChunk> ramp()
  {
    std::vector<int16_t> distances(terrain::samplesPerChunk);
    std::vector<uint8_t> materials(terrain::samplesPerChunk, 2);

    for (int i = 0; i < terrain::samplesPerChunk; ++i)
    {
      distances[static_cast<size_t>(i)] = static_cast<int16_t>(i % 100);
    }

    return terrain::TerrainChunk::dense(std::move(distances), std::move(materials));
  }

  const terrain::SampleBox oneChunk{ { 0, 0, 0 }, { terrain::chunkSize, terrain::chunkSize, terrain::chunkSize } };

  void keep(glm::ivec3, int16_t&, uint8_t&) {}
}

TEST(TerrainOpsTest, CommitAppliesSetAndErase)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ 0, 0, 0 }, solid());

  const auto added = solid(3);
  terrain::commit(volume, { { { 0, 0, 0 }, nullptr }, { { 1, 0, 0 }, added } });

  EXPECT_EQ(volume.chunk({ 0, 0, 0 }), nullptr);
  EXPECT_EQ(volume.chunk({ 1, 0, 0 }), added);
}

TEST(TerrainOpsTest, CommitRefusesAnOutOfRangeBatchWithoutChangingTheVolume)
{
  terrain::TerrainVolume volume;
  const auto existing = solid();
  volume.setChunk({ 0, 0, 0 }, existing);

  const terrain::ChunkEdits valid{ { { 1, 0, 0 }, solid(2) } };
  terrain::ChunkEdits batch = valid;
  batch.push_back({ { terrain::maxChunkCoord + 1, 0, 0 }, solid(2) });

  EXPECT_THROW(terrain::commit(volume, batch), std::out_of_range);
  EXPECT_EQ(volume.chunk({ 1, 0, 0 }), nullptr);
  EXPECT_EQ(volume.chunk({ 0, 0, 0 }), existing);
  EXPECT_EQ(volume.chunks().size(), 1u);

  terrain::commit(volume, valid);
  EXPECT_NE(volume.chunk({ 1, 0, 0 }), nullptr);
}

TEST(TerrainOpsTest, IdentityEditReturnsNothingAndLeavesTheVolumeUntouched)
{
  terrain::TerrainVolume volume;
  const auto dense = ramp();
  volume.setChunk({ 0, 0, 0 }, dense);
  const uint64_t hashBefore = dense->hash();

  EXPECT_TRUE(terrain::editSamples(volume, oneChunk, keep).empty());
  EXPECT_TRUE(terrain::editSamples(volume, { { 64, 0, 0 }, { 70, 4, 4 } }, keep).empty());

  const auto changed = terrain::editSamples(volume, oneChunk, [](const glm::ivec3 sample, int16_t& distance, uint8_t&)
  {
    if (sample == glm::ivec3(3, 4, 5))
    {
      distance = 7777;
    }
  });

  ASSERT_EQ(changed.size(), 1u);
  EXPECT_EQ(changed[0].first, (terrain::ChunkCoord{ 0, 0, 0 }));
  ASSERT_NE(changed[0].second, nullptr);
  EXPECT_EQ(changed[0].second->distanceAt(3, 4, 5), 7777);

  EXPECT_EQ(volume.chunk({ 0, 0, 0 }), dense);
  EXPECT_EQ(volume.chunk({ 0, 0, 0 })->hash(), hashBefore);
  EXPECT_EQ(volume.chunks().size(), 1u);
}

TEST(TerrainOpsTest, TurningAChunkToAirEmitsNull)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ 0, 0, 0 }, ramp());

  const auto edits = terrain::editSamples(volume, oneChunk, [](glm::ivec3, int16_t& distance, uint8_t& material)
  {
    distance = terrain::airDistance;
    material = 0;
  });

  ASSERT_EQ(edits.size(), 1u);
  EXPECT_EQ(edits[0].second, nullptr);
}

TEST(TerrainOpsTest, BoxAtNegativeCoordinatesTouchesEveryChunkItOverlaps)
{
  const terrain::TerrainVolume volume;

  const auto edits = terrain::editSamples(volume, { { -1, -1, -1 }, { 1, 1, 1 } },
                                          [](glm::ivec3, int16_t& distance, uint8_t& material)
  {
    distance = -terrain::airDistance;
    material = 1;
  });

  std::set<std::tuple<int, int, int>> coords;

  for (const auto& edit : edits)
  {
    EXPECT_NE(edit.second, nullptr);
    coords.insert({ edit.first.x, edit.first.y, edit.first.z });
  }

  EXPECT_EQ(edits.size(), 8u);
  EXPECT_EQ(coords.size(), 8u);

  for (const int x : { -1, 0 })
  {
    for (const int y : { -1, 0 })
    {
      for (const int z : { -1, 0 })
      {
        EXPECT_EQ(coords.count({ x, y, z }), 1u);
      }
    }
  }
}

TEST(TerrainOpsTest, OversizedOrOutOfRangeBoxesAreRefusedWithoutRunningTheEdit)
{
  const terrain::TerrainVolume volume;
  bool called = false;

  const auto mark = [&called](glm::ivec3, int16_t& distance, uint8_t&)
  {
    called = true;
    distance = -terrain::airDistance;
  };

  const int big = 1 << 28;
  EXPECT_TRUE(terrain::editSamples(volume, { { -big, -big, -big }, { big, big, big } }, mark).empty());
  EXPECT_TRUE(terrain::editSamples(volume, { { 0, 0, 0 }, { terrain::chunkSize * 200, terrain::chunkSize * 200, 1 } },
                                   mark).empty());

  const int beyond = (terrain::maxChunkCoord + 1) * terrain::chunkSize;
  EXPECT_TRUE(terrain::editSamples(volume, { { beyond, 0, 0 }, { beyond + 1, 1, 1 } }, mark).empty());
  EXPECT_FALSE(called);

  EXPECT_EQ(terrain::editSamples(volume, { { 0, 0, 0 }, { 4, 4, 4 } }, mark).size(), 1u);
  EXPECT_TRUE(called);
}

TEST(TerrainOpsTest, OutOfRangeWritesAreClamped)
{
  const terrain::TerrainVolume volume;

  const auto edits = terrain::editSamples(volume, { { 0, 0, 0 }, { 2, 1, 1 } },
                                          [](const glm::ivec3 sample, int16_t& distance, uint8_t& material)
  {
    distance = sample.x == 0 ? int16_t{ -32768 } : int16_t{ 32767 };
    material = sample.x == 0 ? terrain::reservedMaterial : uint8_t{ 5 };
  });

  ASSERT_EQ(edits.size(), 1u);
  const auto chunk = edits[0].second;
  ASSERT_NE(chunk, nullptr);
  EXPECT_EQ(chunk->distanceAt(0, 0, 0), -terrain::airDistance);
  EXPECT_EQ(chunk->materialAt(0, 0, 0), 0);
  EXPECT_EQ(chunk->distanceAt(1, 0, 0), terrain::airDistance);
  EXPECT_EQ(chunk->materialAt(1, 0, 0), 5);
}

TEST(TerrainOpsTest, UniformHintSkipsWholeChunksAndMatchesTheSampleEdit)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ 1, 0, 0 }, ramp());
  const terrain::SampleBox box{ { 0, 0, 0 }, { terrain::chunkSize * 2, terrain::chunkSize, terrain::chunkSize } };
  int visited = 0;

  const auto edit = [&visited](glm::ivec3, int16_t& distance, uint8_t& material)
  {
    ++visited;
    distance = -terrain::airDistance;
    material = 4;
  };
  const auto hint = [](const terrain::ChunkCoord, int16_t& distance, uint8_t& material)
  {
    distance = -terrain::airDistance;
    material = 4;
    return true;
  };

  const auto hinted = terrain::editSamples(volume, box, edit, hint);
  EXPECT_EQ(visited, 0);

  const auto sampled = terrain::editSamples(volume, box, edit);
  EXPECT_EQ(visited, 2 * terrain::samplesPerChunk);

  ASSERT_EQ(hinted.size(), sampled.size());
  for (size_t index = 0; index < hinted.size(); ++index)
  {
    EXPECT_EQ(hinted[index].first, sampled[index].first);
    EXPECT_EQ(hinted[index].second->hash(), sampled[index].second->hash());
  }
}

TEST(TerrainOpsTest, ParallelEditsMatchSerialOnes)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ 1, 0, 0 }, ramp());
  const terrain::SampleBox box{ { -10, 0, 0 }, { terrain::chunkSize * 4, terrain::chunkSize * 2, terrain::chunkSize } };

  const auto edit = [](const glm::ivec3 sample, int16_t& distance, uint8_t& material)
  {
    distance = static_cast<int16_t>((sample.x * 31 + sample.y * 17 + sample.z * 7) % 400 - 200);
    material = static_cast<uint8_t>(sample.x % 5);
  };

  const auto serial = terrain::editSamples(volume, box, edit);
  const auto parallel = terrain::editSamples(volume, box, edit, {}, {}, true);

  ASSERT_EQ(serial.size(), parallel.size());
  for (size_t index = 0; index < serial.size(); ++index)
  {
    EXPECT_EQ(serial[index].first, parallel[index].first);
    ASSERT_NE(serial[index].second, nullptr);
    ASSERT_NE(parallel[index].second, nullptr);
    EXPECT_EQ(serial[index].second->hash(), parallel[index].second->hash());
  }
}

TEST(TerrainOpsTest, ParallelForRunsEveryIndexOnceAndRethrows)
{
  std::vector<std::atomic<int>> hits(257);

  for (int round = 0; round < 20; ++round)
  {
    terrain::parallelFor(static_cast<int>(hits.size()), [&hits](const int index)
    {
      ++hits[static_cast<size_t>(index)];
    });
  }

  for (const auto& hit : hits)
  {
    EXPECT_EQ(hit.load(), 20);
  }

  EXPECT_THROW(terrain::parallelFor(64, [](const int index)
  {
    if (index == 13)
    {
      throw std::runtime_error("boom");
    }
  }), std::runtime_error);

  terrain::parallelFor(0, [](int) { FAIL(); });
}
