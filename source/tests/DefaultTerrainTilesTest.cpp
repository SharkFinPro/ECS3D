#include <gtest/gtest.h>

#include "DefaultTerrainTiles.h"
#include "DefaultTerrainTilesFixture.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainRaycast.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <set>
#include <utility>
#include <vector>

using basintest::DefaultTerrainTilesTest;
using basintest::Seam;
using basintest::Tiles;
using basintest::lastColumn;
using basintest::overlapColumns;
using basintest::tileIndex;

namespace {
  constexpr float voxel = basin::voxelSize;

  // The materials of the solid samples inside the distance band, which is where the surface reads them.
  void addSurfaceMaterials(const terrain::TerrainVolume& volume, std::set<uint8_t>& materials)
  {
    for (const auto& [coord, chunk] : volume.chunks())
    {
      if (chunk->isUniform())
      {
        continue;
      }

      for (int index = 0; index < terrain::samplesPerChunk; ++index)
      {
        const int16_t distance = chunk->distanceAtIndex(index);
        if (distance < 0 && distance > -terrain::airDistance)
        {
          materials.insert(chunk->materialAtIndex(index));
        }
      }
    }
  }

  // The world height of the ground a ray straight down from above finds at a tile-local column, or nothing.
  std::optional<float> hitHeight(const terrain::TerrainVolume& volume, const int row, const int col, const float x,
                                 const float z)
  {
    const glm::vec3 origin(x * voxel, static_cast<float>(basin::maxHeight) * voxel + 10.0f, z * voxel);
    const auto hit = terrain::raycast(volume, origin, { 0.0f, -1.0f, 0.0f }, 1000.0f);

    return hit ? std::optional<float>(hit->point.y + basin::tileOrigin(row, col).y) : std::nullopt;
  }

  // The tallest y a sample on a tile's edge can have in either tile; every sample above it is air in both.
  int topSample(const Tiles& tiles, const Seam& seam)
  {
    return std::max(tiles[seam.a()]->occupiedBounds().max.y, tiles[seam.b()]->occupiedBounds().max.y);
  }

  // How many samples of the three shared columns (the seam and its two aprons) differ between the two tiles of a seam.
  int seamMismatches(const Tiles& tiles, const Seam& seam)
  {
    const auto& a = *tiles[seam.a()];
    const auto& b = *tiles[seam.b()];
    const int top = topSample(tiles, seam);

    int mismatches = 0;
    for (int shared = 0; shared < overlapColumns; ++shared)
    {
      const int inA = basin::tileStride + shared;

      for (int along = 0; along <= lastColumn; ++along)
      {
        for (int y = a.occupiedBounds().min.y; y < top; ++y)
        {
          const int16_t ours = seam.east ? a.distanceAtSample({ inA, y, along })
                                         : a.distanceAtSample({ along, y, inA });
          const int16_t theirs = seam.east ? b.distanceAtSample({ shared, y, along })
                                           : b.distanceAtSample({ along, y, shared });
          mismatches += ours != theirs ? 1 : 0;
        }
      }
    }

    return mismatches;
  }

  // The ground a ray straight down finds on the seam column from each tile (a's second to last column is b's second),
  // and from one voxel inside the first tile.
  struct SeamRays
  {
    std::optional<float> ours;
    std::optional<float> theirs;
    std::optional<float> inside;
  };

  SeamRays raysAt(const Tiles& tiles, const Seam& seam, const float along)
  {
    const float seamInA = static_cast<float>(basin::tileStride + 1);
    const auto& a = *tiles[seam.a()];
    const auto& b = *tiles[seam.b()];

    return {
      hitHeight(a, seam.rowA, seam.colA, seam.east ? seamInA : along, seam.east ? along : seamInA),
      hitHeight(b, seam.rowB, seam.colB, seam.east ? 1.0f : along, seam.east ? along : 1.0f),
      hitHeight(a, seam.rowA, seam.colA, seam.east ? seamInA - 1.0f : along, seam.east ? along : seamInA - 1.0f)
    };
  }
}

TEST(DefaultTerrainTilesLayoutTest, TilesOverlapByThreeColumnsAndTheBasinStaysCentered)
{
  EXPECT_EQ(basin::tileColumns, 128);
  EXPECT_EQ(basin::tileStride, basin::tileColumns - 3);
  EXPECT_EQ(basin::worldColumns, basin::tilesPerSide * basin::tileStride + 3);
  EXPECT_EQ(overlapColumns, 3);

  // The drawn columns, 1 to worldColumns - 2, are centered on the world origin.
  const float drawnCenter = (1.0f + static_cast<float>(basin::worldColumns - 2)) * 0.5f;
  EXPECT_NEAR(basin::tileOrigin(0, 0).x + drawnCenter * basin::voxelSize, 0.0f, 1e-3f);
  EXPECT_NEAR(basin::tileOrigin(0, 0).z + drawnCenter * basin::voxelSize, 0.0f, 1e-3f);
  EXPECT_NEAR(basin::tileOrigin(1, 2).x - basin::tileOrigin(1, 1).x,
              static_cast<float>(basin::tileStride) * basin::voxelSize, 1e-3f);
}

TEST_F(DefaultTerrainTilesTest, NeighborsShareTheirEdgeSamplesExactly)
{
  int seams = 0;
  basintest::forEachSeam([&](const Seam& seam)
  {
    SCOPED_TRACE(seam.label());

    // Both tiles start at the same height, so one loop over y compares the same samples.
    EXPECT_EQ(s_tiles[seam.a()]->occupiedBounds().min.y, s_tiles[seam.b()]->occupiedBounds().min.y);
    EXPECT_EQ(seamMismatches(s_tiles, seam), 0);
    ++seams;
  });

  EXPECT_EQ(seams, 12);
}

TEST_F(DefaultTerrainTilesTest, TheColumnInsideAnEdgeDiffersFromTheEdge)
{
  // The positive control for the seam check: it can tell two different columns apart, and each of the three shared
  // columns differs from the next.
  const auto& tile = *s_tiles[tileIndex(1, 1)];
  const int top = tile.occupiedBounds().max.y;

  int differing = 0;
  for (int z = 0; z <= lastColumn; ++z)
  {
    for (int y = 0; y < top; ++y)
    {
      differing += tile.distanceAtSample({ lastColumn, y, z }) != tile.distanceAtSample({ lastColumn - 1, y, z }) ? 1 : 0;
    }
  }

  EXPECT_GT(differing, 100);
}

TEST_F(DefaultTerrainTilesTest, ARayStraightDownOnASeamHitsTheSameHeightFromBothTiles)
{
  // A vertical ray finds the zero crossing whatever the edge's slope scaling, so this guards the tiles' placement and
  // stride; the exact-sample test above guards the rewrite of the edge columns.
  const float columns[] = { 8.0f, 37.5f, 64.0f, 99.0f, 120.25f };
  int compared = 0;
  float widestStep = 0.0f;

  basintest::forEachSeam([&](const Seam& seam)
  {
    for (const float along : columns)
    {
      const auto rays = raysAt(s_tiles, seam, along);
      ASSERT_TRUE(rays.ours.has_value() && rays.theirs.has_value() && rays.inside.has_value());
      EXPECT_NEAR(*rays.ours, *rays.theirs, 0.01f) << seam.label() << " at " << along;
      ++compared;

      // One voxel in from the edge the ground is somewhere else.
      widestStep = std::max(widestStep, std::abs(*rays.inside - *rays.ours));
    }
  });

  EXPECT_EQ(compared, 12 * 5);
  EXPECT_GT(widestStep, 0.1f);
}


TEST_F(DefaultTerrainTilesTest, TheChunkCountStaysWithinWhatEveryJoinStreams)
{
  EXPECT_LE(totalChunks(), 600u);
  EXPECT_GE(totalChunks(), static_cast<size_t>(basin::tileCount) * 16u);
}

TEST_F(DefaultTerrainTilesTest, GenerationStaysQuick)
{
#ifndef NDEBUG
  GTEST_SKIP() << "A Release measurement: Debug, sanitizer and coverage builds skip it";
#endif
  std::cout << "[ INFO ] basin generation took " << s_seconds << " s for " << totalChunks() << " chunks\n";
  EXPECT_LT(s_seconds, 3.0);
}

namespace {
  float height(const int x, const int z)
  {
    return basin::groundAt(static_cast<float>(x), static_cast<float>(z));
  }

  // How many flat runs of at least four columns stand above the arena floor, each at its own height, along a line.
  int plateausAlong(const std::function<float(int)>& heightAt, const int from, const int to)
  {
    std::vector<float> plateaus;
    int run = 0;
    float start = 0.0f;

    const auto close = [&]()
    {
      const bool high = start > static_cast<float>(basin::groundHeight) + 3.0f;
      const bool fresh = std::ranges::none_of(plateaus, [start](const float other)
      {
        return std::abs(other - start) < 3.0f;
      });
      if (run >= 4 && high && fresh)
      {
        plateaus.push_back(start);
      }

      run = 0;
    };

    for (int i = from; i < to; ++i)
    {
      if (std::abs(heightAt(i + 1) - heightAt(i)) < 0.05f)
      {
        start = run == 0 ? heightAt(i) : start;
        ++run;
      }
      else
      {
        close();
      }
    }

    close();

    return static_cast<int>(plateaus.size());
  }

  // The largest second difference at the mogul period's half step over a 40-column patch: zero on a straight slope,
  // large on a field of bumps.
  float bumpiness(const int x0, const int z0)
  {
    float largest = 0.0f;
    for (int z = z0; z < z0 + 40; ++z)
    {
      for (int x = x0; x < x0 + 40; ++x)
      {
        const float alongX = height(x + 8, z) + height(x - 8, z) - 2.0f * height(x, z);
        const float alongZ = height(x, z + 8) + height(x, z - 8) - 2.0f * height(x, z);
        largest = std::max({ largest, std::abs(alongX), std::abs(alongZ) });
      }
    }

    return largest;
  }

  float steepestStep(const int x0, const int x1, const int z0, const int z1)
  {
    float steepest = 0.0f;
    for (int z = z0; z < z1; ++z)
    {
      for (int x = x0; x < x1; ++x)
      {
        steepest = std::max(steepest, std::abs(height(x + 1, z) - height(x, z)));
      }
    }

    return steepest;
  }
}

TEST_F(DefaultTerrainTilesTest, TheArenaIsFlatAndTheRimRisesFarAboveIt)
{
  const float centerColumn = static_cast<float>(basin::worldColumns - 1) / 2.0f;
  float lowest = 1e9f;
  float highest = -1e9f;

  for (int z = 0; z < basin::worldColumns; ++z)
  {
    for (int x = 0; x < basin::worldColumns; ++x)
    {
      if (std::hypot(static_cast<float>(x) - centerColumn, static_cast<float>(z) - centerColumn) < 40.0f)
      {
        lowest = std::min(lowest, height(x, z));
        highest = std::max(highest, height(x, z));
      }
    }
  }

  EXPECT_LT(highest - lowest, 1.0f);
  EXPECT_NEAR(lowest, static_cast<float>(basin::groundHeight), 1.0f);

  const int edge = basin::worldColumns - 1;
  for (const auto& [x, z] : { std::pair{ 0, 0 }, std::pair{ edge, 0 }, std::pair{ 0, edge }, std::pair{ edge, edge } })
  {
    EXPECT_GE(height(x, z), static_cast<float>(basin::groundHeight) + 30.0f) << x << "," << z;
  }
}

TEST_F(DefaultTerrainTilesTest, TheVolumeHoldsTheGroundTheFunctionDescribes)
{
  // The raycast lands within a voxel of groundAt: on the arena, on a terrace, on the bank and in the riverbed.
  const std::pair<int, int> columns[] = { { 190, 190 }, { 190, 300 }, { 300, 190 }, { 60, 200 }, { 60, 60 } };

  for (const auto& [x, z] : columns)
  {
    const int col = std::min(x / basin::tileStride, basin::tilesPerSide - 1);
    const int row = std::min(z / basin::tileStride, basin::tilesPerSide - 1);
    const auto hit = hitHeight(*s_tiles[tileIndex(row, col)], row, col, static_cast<float>(x - col * basin::tileStride),
                               static_cast<float>(z - row * basin::tileStride));
    ASSERT_TRUE(hit.has_value()) << x << "," << z;

    const float expected = (height(x, z) - static_cast<float>(basin::groundHeight)) * voxel;
    EXPECT_NEAR(*hit, expected, voxel) << x << "," << z;
  }
}

TEST_F(DefaultTerrainTilesTest, TheNorthStepsIntoTerracesAndTheOtherSidesDoNot)
{
  const auto line = [](const int x) { return [x](const int z) { return height(x, z); }; };

  // Row 2 across all three tiles.
  for (const int x : { 60, 190, 320 })
  {
    EXPECT_GE(plateausAlong(line(x), 250, 376), 3) << x;
  }

  // Positive control: the same rim to the south climbs in one smooth slope.
  EXPECT_EQ(plateausAlong(line(190), 10, 120), 0);
}

TEST_F(DefaultTerrainTilesTest, TheRiverbedCutsBelowBothBanksAcrossTheWestSeam)
{
  const int seam = basin::tileStride;
  int bedZ = 150;
  for (int z = 150; z < 230; ++z)
  {
    bedZ = height(seam, z) < height(seam, bedZ) ? z : bedZ;
  }

  const float bed = height(seam, bedZ);
  EXPECT_LE(bed + 5.0f, std::min(height(seam, bedZ - 14), height(seam, bedZ + 14)));

  // Positive control: on the same seam to the south the ground is a plain slope, with no channel in it.
  const float control = height(seam, 60);
  EXPECT_GT(control + 5.0f, std::min(height(seam, 60 - 14), height(seam, 60 + 14)));
}

TEST_F(DefaultTerrainTilesTest, TheEastBankIsSteepAndTheSouthRimIsNot)
{
  EXPECT_GT(steepestStep(250, 376, 0, 250), 1.5f);

  // Positive control: the south-center tile's rim is a gentle slope.
  EXPECT_LT(steepestStep(125, 249, 0, 124), 1.0f);
}

TEST_F(DefaultTerrainTilesTest, MogulsBumpTheSouthWestAndNowhereElse)
{
  // Peak to trough, a mogul field swings the ground by twice its amplitude, so the second difference reaches four
  // times it.
  EXPECT_GE(bumpiness(20, 20), 8.0f);

  // Positive control: the south-east tile has the same hills and slope without the bumps.
  EXPECT_LT(bumpiness(260, 12), 4.0f);
}

TEST_F(DefaultTerrainTilesTest, TheSurfaceUsesSeveralMaterialsAndNeverTheReservedOne)
{
  std::set<uint8_t> materials;
  for (const auto& tile : s_tiles)
  {
    addSurfaceMaterials(*tile, materials);
  }

  EXPECT_TRUE(materials.contains(0));
  EXPECT_TRUE(materials.contains(1));
  EXPECT_TRUE(materials.contains(2));
  EXPECT_TRUE(materials.contains(4));
  EXPECT_FALSE(materials.contains(terrain::reservedMaterial));
}
