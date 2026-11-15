#include <gtest/gtest.h>

#include "terrain/TerrainChunk.h"
#include "terrain/TerrainGenerators.h"
#include "terrain/TerrainOps.h"
#include "terrain/TerrainSampling.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace {
  constexpr float voxel = 2.5f;

  float distanceAt(const terrain::TerrainVolume& volume, const int x, const int y, const int z)
  {
    return terrain::decodeDistance(volume.distanceAtSample({ x, y, z }));
  }

  // Heights in terrain units: the scale below maps a pixel value to the same number.
  constexpr float pixelScale = 65535.0f;

  std::vector<uint16_t> planePixels(const int width, const int rows, const int perU, const int perV)
  {
    std::vector<uint16_t> pixels(static_cast<size_t>(width * rows));

    for (int v = 0; v < rows; ++v)
    {
      for (int u = 0; u < width; ++u)
      {
        pixels[static_cast<size_t>(v * width + u)] = static_cast<uint16_t>(perU * u + perV * v);
      }
    }

    return pixels;
  }
}

TEST(TerrainGeneratorsTest, FillFlatMatchesTheAnalyticDistanceInTheBand)
{
  terrain::TerrainVolume volume(voxel);
  terrain::commit(volume, terrain::fillFlat(volume, { 64, 64 }, 10.0f, 20.0f, 4));
  ASSERT_FALSE(volume.chunks().empty());

  terrain::Sampler sampler(volume);

  for (const glm::vec3 local : { glm::vec3(40.0f, 11.0f, 40.0f), glm::vec3(40.0f, 8.0f, 100.0f),
                                 glm::vec3(100.0f, 12.5f, 60.0f), glm::vec3(20.0f, 5.0f, 30.0f) })
  {
    EXPECT_NEAR(sampler.distance(local), (local.y - 10.0f) / voxel, 2e-4f);
  }

  EXPECT_EQ(volume.materialAtSample({ 16, 3, 16 }), 4);
  EXPECT_LT(distanceAt(volume, 16, 3, 16), 0.0f);
  EXPECT_GT(distanceAt(volume, 16, 6, 16), 0.0f);
}

TEST(TerrainGeneratorsTest, FillFlatEndsAtAChunkBoundaryAndStaysInTheFootprint)
{
  terrain::TerrainVolume volume(voxel);
  terrain::commit(volume, terrain::fillFlat(volume, { 64, 64 }, 10.0f, 20.0f, 4));

  int lowestLayer = std::numeric_limits<int>::max();
  bool foundDeepSolid = false;

  for (const auto& [coord, chunk] : volume.chunks())
  {
    lowestLayer = std::min(lowestLayer, coord.y);
    EXPECT_TRUE(coord.x == 0 || coord.x == 1);
    EXPECT_TRUE(coord.z == 0 || coord.z == 1);

    if (coord.y < 0)
    {
      foundDeepSolid = true;
      EXPECT_TRUE(chunk->isUniform());
      EXPECT_FALSE(chunk->isAir());
    }
  }

  EXPECT_TRUE(foundDeepSolid);
  EXPECT_EQ(lowestLayer, -1);
  EXPECT_EQ(volume.chunks().size(), 8u);
}

TEST(TerrainGeneratorsTest, FillFlatRefusesInvalidArguments)
{
  const terrain::TerrainVolume volume(voxel);
  const float nan = std::numeric_limits<float>::quiet_NaN();

  EXPECT_FALSE(terrain::fillFlat(volume, { 64, 64 }, 10.0f, 20.0f, 4).empty());

  EXPECT_TRUE(terrain::fillFlat(volume, { 64, 64 }, 10.0f, 0.0f, 4).empty());
  EXPECT_TRUE(terrain::fillFlat(volume, { 64, 64 }, nan, 20.0f, 4).empty());
  EXPECT_TRUE(terrain::fillFlat(volume, { 64, 64 }, 10.0f, nan, 4).empty());
  EXPECT_TRUE(terrain::fillFlat(volume, { 0, 64 }, 10.0f, 20.0f, 4).empty());
  EXPECT_TRUE(terrain::fillFlat(volume, { 64, 0 }, 10.0f, 20.0f, 4).empty());
  EXPECT_TRUE(terrain::fillFlat(volume, { 64, 64 }, 10.0f, -5.0f, 4).empty());
  EXPECT_TRUE(terrain::fillFlat(volume, { 64, 64 }, 10.0f, 20.0f, terrain::reservedMaterial).empty());
  EXPECT_TRUE(terrain::fillFlat(volume, { 1 << 20, 1 << 20 }, 10.0f, 20.0f, 4).empty());
}

TEST(TerrainGeneratorsTest, ConstantHeightmapMatchesFillFlat)
{
  terrain::TerrainVolume flat(voxel);
  terrain::commit(flat, terrain::fillFlat(flat, { 64, 64 }, 10.0f, 20.0f, 4));

  const std::vector<uint16_t> pixels(64 * 64, 1000);
  terrain::TerrainVolume mapped(voxel);
  const auto edits = terrain::fillFromHeightmap(mapped, { pixels, 64, 64 }, 655.35f, 20.0f, 4);
  ASSERT_FALSE(edits.empty());
  terrain::commit(mapped, edits);

  EXPECT_EQ(mapped.chunks().size(), flat.chunks().size());

  for (int z = 0; z < 64; z += 7)
  {
    for (int y = -32; y < 9; ++y)
    {
      for (int x = 0; x < 64; x += 5)
      {
        EXPECT_LE(std::abs(mapped.distanceAtSample({ x, y, z }) - flat.distanceAtSample({ x, y, z })), 2);
        EXPECT_EQ(mapped.materialAtSample({ x, y, z }), flat.materialAtSample({ x, y, z }));
      }
    }
  }
}

TEST(TerrainGeneratorsTest, RampHeightmapMatchesTheAnalyticDistance)
{
  std::vector<uint16_t> pixels(64 * 64);

  for (int v = 0; v < 64; ++v)
  {
    for (int u = 0; u < 64; ++u)
    {
      pixels[static_cast<size_t>(v * 64 + u)] = static_cast<uint16_t>(100 * u);
    }
  }

  terrain::TerrainVolume volume(voxel);
  const auto edits = terrain::fillFromHeightmap(volume, { pixels, 64, 64 }, 655.35f, 10.0f, 4);
  ASSERT_FALSE(edits.empty());
  terrain::commit(volume, edits);

  const float slope = 1.0f / voxel;
  const float norm = std::sqrt(1.0f + slope * slope);
  int checked = 0;

  for (const int x : { 5, 20, 40, 58 })
  {
    for (const int y : { static_cast<int>(x / voxel) - 1, static_cast<int>(x / voxel), static_cast<int>(x / voxel) + 1 })
    {
      const float expected = (static_cast<float>(y) * voxel - static_cast<float>(x)) / norm / voxel;
      EXPECT_NEAR(distanceAt(volume, x, y, 30), expected, 5e-4f);
      ++checked;
    }
  }

  EXPECT_EQ(checked, 12);
  EXPECT_LT(distanceAt(volume, 20, 4, 30), 0.0f);
  EXPECT_GT(distanceAt(volume, 20, 12, 30), 0.0f);
}

TEST(TerrainGeneratorsTest, HeightmapRefusesInvalidArguments)
{
  const terrain::TerrainVolume volume(voxel);
  const std::vector<uint16_t> pixels(16 * 16, 100);

  EXPECT_FALSE(terrain::fillFromHeightmap(volume, { pixels, 16, 16 }, 100.0f, 10.0f, 4).empty());

  EXPECT_TRUE(terrain::fillFromHeightmap(volume, { pixels, 16, 15 }, 100.0f, 10.0f, 4).empty());
  EXPECT_TRUE(terrain::fillFromHeightmap(volume, { {}, 0, 16 }, 100.0f, 10.0f, 4).empty());
  EXPECT_TRUE(terrain::fillFromHeightmap(volume, { pixels, 16, 16 }, -1.0f, 10.0f, 4).empty());
  EXPECT_TRUE(terrain::fillFromHeightmap(volume, { pixels, 16, 16 }, 100.0f, 0.0f, 4).empty());
  EXPECT_TRUE(terrain::fillFromHeightmap(volume, { pixels, 16, 16 }, std::numeric_limits<float>::quiet_NaN(), 10.0f, 4)
                .empty());
  EXPECT_TRUE(terrain::fillFromHeightmap(volume, { pixels, 16, 16 }, std::numeric_limits<float>::infinity(), 10.0f, 4)
                .empty());
  EXPECT_TRUE(terrain::fillFromHeightmap(volume, { pixels, 16, 16 }, 100.0f, std::numeric_limits<float>::quiet_NaN(),
                                         4).empty());
  EXPECT_TRUE(terrain::fillFromHeightmap(volume, { pixels, 16, 16 }, 100.0f, 10.0f, terrain::reservedMaterial)
                .empty());
  EXPECT_TRUE(terrain::fillFromHeightmap(volume, { pixels, 16, 0 }, 100.0f, 10.0f, 4).empty());
}

TEST(TerrainGeneratorsTest, GeneratingKeepsSamplesOutsideTheFootprint)
{
  std::vector<int16_t> distances(terrain::samplesPerChunk, 123);
  distances[0] = 124;

  terrain::TerrainVolume volume(voxel);
  volume.setChunk({ 1, 0, 0 }, terrain::TerrainChunk::dense(std::move(distances),
                                                             std::vector<uint8_t>(terrain::samplesPerChunk, 7)));

  terrain::commit(volume, terrain::fillFlat(volume, { 40, 40 }, 10.0f, 20.0f, 4));

  EXPECT_EQ(volume.distanceAtSample({ 50, 3, 3 }), 123);
  EXPECT_EQ(volume.materialAtSample({ 50, 3, 3 }), 7);
  EXPECT_NE(volume.distanceAtSample({ 35, 3, 3 }), 123);
  EXPECT_EQ(volume.materialAtSample({ 35, 3, 3 }), 4);
}

TEST(TerrainGeneratorsTest, SteepHeightmapLeavesNoJumpAtTheTopOfTheWrittenExtent)
{
  const auto pixels = planePixels(48, 32, 5, 0);
  terrain::TerrainVolume volume(voxel);
  terrain::commit(volume, terrain::fillFromHeightmap(volume, { pixels, 48, 32 }, pixelScale, 10.0f, 4));
  ASSERT_FALSE(volume.chunks().empty());

  for (int z = 0; z < 32; z += 5)
  {
    for (int x = 0; x < 48; ++x)
    {
      for (int y = -32; y < 110; ++y)
      {
        EXPECT_LE(std::abs(distanceAt(volume, x, y + 1, z) - distanceAt(volume, x, y, z)), 1.0f + 1e-3f)
          << x << " " << y << " " << z;
      }
    }
  }

  EXPECT_EQ(volume.distanceAtSample({ 47, 110, 0 }), terrain::airDistance);
  EXPECT_NEAR(distanceAt(volume, 47, 94, 0), 0.0f, 5e-4f);
}

TEST(TerrainGeneratorsTest, NonSquareHeightmapCoversItsColumnsAndRows)
{
  const auto pixels = planePixels(48, 32, 5, 0);
  terrain::TerrainVolume volume(voxel);
  terrain::commit(volume, terrain::fillFromHeightmap(volume, { pixels, 48, 32 }, pixelScale, 10.0f, 4));

  bool sawChunkX0 = false;
  bool sawChunkX1 = false;

  for (const auto& [coord, chunk] : volume.chunks())
  {
    EXPECT_EQ(coord.z, 0);
    sawChunkX0 = sawChunkX0 || coord.x == 0;
    sawChunkX1 = sawChunkX1 || coord.x == 1;
    EXPECT_LE(coord.x, 1);
  }

  EXPECT_TRUE(sawChunkX0);
  EXPECT_TRUE(sawChunkX1);

  const float norm = std::sqrt(5.0f);
  EXPECT_NEAR(distanceAt(volume, 47, 94, 31), 0.0f, 5e-4f);
  EXPECT_NEAR(distanceAt(volume, 47, 95, 31), 1.0f / norm, 5e-4f);
  EXPECT_NEAR(distanceAt(volume, 10, 20, 31), 0.0f, 5e-4f);
  EXPECT_EQ(volume.distanceAtSample({ 10, 20, 32 }), terrain::airDistance);
}

TEST(TerrainGeneratorsTest, TiltedPlaneMatchesTheAnalyticDistanceInBothDirections)
{
  const auto pixels = planePixels(48, 32, 3, 5);
  terrain::TerrainVolume volume(voxel);
  terrain::commit(volume, terrain::fillFromHeightmap(volume, { pixels, 48, 32 }, pixelScale, 10.0f, 4));

  const float norm = std::sqrt(1.0f + 1.2f * 1.2f + 2.0f * 2.0f);
  int checked = 0;

  for (const auto [x, z] : { std::pair{ 20, 15 }, std::pair{ 30, 5 } })
  {
    const float height = static_cast<float>(3 * x + 5 * z);

    for (const int offset : { -1, 0, 1 })
    {
      const int y = static_cast<int>(height / voxel) + offset;
      EXPECT_NEAR(distanceAt(volume, x, y, z), (static_cast<float>(y) * voxel - height) / norm / voxel, 5e-4f);
      ++checked;
    }
  }

  EXPECT_EQ(checked, 6);
}

TEST(TerrainGeneratorsTest, GeneratingPreservesSamplesAboveBelowAndBesideTheFootprint)
{
  std::vector<int16_t> distances(terrain::samplesPerChunk, 123);
  distances[0] = 124;

  terrain::TerrainVolume volume(voxel);

  for (const terrain::ChunkCoord coord : { terrain::ChunkCoord{ 0, 0, 0 }, terrain::ChunkCoord{ 0, -2, 0 } })
  {
    volume.setChunk(coord, terrain::TerrainChunk::dense(distances, std::vector<uint8_t>(terrain::samplesPerChunk, 7)));
  }

  terrain::commit(volume, terrain::fillFlat(volume, { 20, 20 }, 10.0f, 20.0f, 4));

  EXPECT_EQ(volume.distanceAtSample({ 5, 20, 5 }), 123);
  EXPECT_EQ(volume.distanceAtSample({ 5, 3, 25 }), 123);
  EXPECT_EQ(volume.distanceAtSample({ 5, -40, 5 }), 123);
  EXPECT_EQ(volume.materialAtSample({ 5, 3, 25 }), 7);
  EXPECT_NE(volume.distanceAtSample({ 5, 3, 5 }), 123);
  EXPECT_EQ(volume.materialAtSample({ 5, 3, 5 }), 4);
}

TEST(TerrainGeneratorsTest, RegeneratingTheSameTerrainChangesNothing)
{
  const auto pixels = planePixels(48, 32, 5, 2);
  terrain::TerrainVolume flat(voxel);
  terrain::TerrainVolume mapped(voxel);

  const auto flatEdits = terrain::fillFlat(flat, { 64, 64 }, 10.0f, 20.0f, 4);
  const auto mappedEdits = terrain::fillFromHeightmap(mapped, { pixels, 48, 32 }, pixelScale, 10.0f, 4);
  ASSERT_FALSE(flatEdits.empty());
  ASSERT_FALSE(mappedEdits.empty());
  terrain::commit(flat, flatEdits);
  terrain::commit(mapped, mappedEdits);

  EXPECT_TRUE(terrain::fillFlat(flat, { 64, 64 }, 10.0f, 20.0f, 4).empty());
  EXPECT_TRUE(terrain::fillFromHeightmap(mapped, { pixels, 48, 32 }, pixelScale, 10.0f, 4).empty());
}
