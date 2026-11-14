#include <gtest/gtest.h>

#include "terrain/TerrainChunk.h"
#include "terrain/TerrainCodec.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
  constexpr float voxelSize = 2.5f;
  constexpr int columns = 1024;
  constexpr double bytesPerMiB = 1024.0 * 1024.0;

  double heightAt(const double x, const double z)
  {
    return 40.0 * std::sin(x / 400.0) * std::cos(z / 300.0) + 15.0 * std::sin(x / 130.0 + z / 170.0);
  }

  // Slope factor cos(atan(|grad h|)): scales the vertical distance to the surface into a perpendicular one.
  double slopeFactorAt(const double x, const double z)
  {
    const double dhdx = 40.0 * std::cos(x / 400.0) * std::cos(z / 300.0) / 400.0 +
                        15.0 * std::cos(x / 130.0 + z / 170.0) / 130.0;
    const double dhdz = -40.0 * std::sin(x / 400.0) * std::sin(z / 300.0) / 300.0 +
                        15.0 * std::cos(x / 130.0 + z / 170.0) / 170.0;
    return 1.0 / std::sqrt(1.0 + dhdx * dhdx + dhdz * dhdz);
  }

  struct Grid
  {
    std::vector<float> height;
    std::vector<float> slope;
  };

  Grid buildGrid()
  {
    Grid grid;
    grid.height.resize(static_cast<size_t>(columns) * columns);
    grid.slope.resize(grid.height.size());

    for (int k = 0; k < columns; ++k)
    {
      for (int i = 0; i < columns; ++i)
      {
        const double x = static_cast<double>(i) * voxelSize;
        const double z = static_cast<double>(k) * voxelSize;
        const size_t at = static_cast<size_t>(k) * columns + static_cast<size_t>(i);
        grid.height[at] = static_cast<float>(heightAt(x, z));
        grid.slope[at] = static_cast<float>(slopeFactorAt(x, z));
      }
    }

    return grid;
  }

  // Solid below the rolling surface; the distance is the slope corrected vertical one. Nothing is solid below
  // the lowest held chunk layer, which is simply absent.
  float distanceInVoxels(const float height, const float slope, const float yUnits)
  {
    return (yUnits - height) * slope / voxelSize;
  }

  // Lowest sample row, over every column, with a sample inside the band (|d| < 4 voxels before clamping).
  int lowestBandSample(const Grid& grid)
  {
    int lowest = std::numeric_limits<int>::max();

    for (size_t at = 0; at < grid.height.size(); ++at)
    {
      const float reach = terrain::bandVoxels * voxelSize / grid.slope[at];
      lowest = std::min(lowest, static_cast<int>(std::floor((grid.height[at] - reach) / voxelSize)) + 1);
    }

    return lowest;
  }

  struct ColumnRange
  {
    float lowestHeight = 0.0f;
    float highestHeight = 0.0f;
    float smallestSlope = 1.0f;
  };

  ColumnRange rangeOfColumn(const Grid& grid, const int chunkX, const int chunkZ)
  {
    ColumnRange range;
    range.lowestHeight = 1.0e9f;
    range.highestHeight = -1.0e9f;

    for (int z = 0; z < terrain::chunkSize; ++z)
    {
      for (int x = 0; x < terrain::chunkSize; ++x)
      {
        const size_t at = static_cast<size_t>(chunkZ * terrain::chunkSize + z) * columns +
                          static_cast<size_t>(chunkX * terrain::chunkSize + x);
        range.lowestHeight = std::min(range.lowestHeight, grid.height[at]);
        range.highestHeight = std::max(range.highestHeight, grid.height[at]);
        range.smallestSlope = std::min(range.smallestSlope, grid.slope[at]);
      }
    }

    return range;
  }

  std::shared_ptr<const terrain::TerrainChunk> buildChunk(const Grid& grid, const int chunkX, const int chunkY,
                                                          const int chunkZ)
  {
    std::vector<int16_t> distances(terrain::samplesPerChunk);
    std::vector<uint8_t> materials(terrain::samplesPerChunk, 0);

    for (int z = 0; z < terrain::chunkSize; ++z)
    {
      for (int x = 0; x < terrain::chunkSize; ++x)
      {
        const size_t at = static_cast<size_t>(chunkZ * terrain::chunkSize + z) * columns +
                          static_cast<size_t>(chunkX * terrain::chunkSize + x);

        for (int y = 0; y < terrain::chunkSize; ++y)
        {
          const float yUnits = static_cast<float>(chunkY * terrain::chunkSize + y) * voxelSize;
          distances[static_cast<size_t>(terrain::sampleIndex(x, y, z))] =
            terrain::encodeDistance(distanceInVoxels(grid.height[at], grid.slope[at], yUnits));
        }
      }
    }

    return terrain::TerrainChunk::dense(std::move(distances), std::move(materials));
  }

  // Number of 16-sample blocks the band (|d| < 4 voxels before clamping) crosses, in the same region.
  size_t denseBlocksOfSixteen(const Grid& grid)
  {
    constexpr int blockSize = 16;
    constexpr int blocksPerSide = columns / blockSize;
    constexpr int blockYOffset = 8;
    constexpr int blockYCount = 16;
    std::vector<bool> crossed(static_cast<size_t>(blocksPerSide) * blocksPerSide * blockYCount, false);

    const auto mark = [&](const int blockX, const int blockZ, const int lowSample, const int highSample)
    {
      if (lowSample > highSample)
      {
        return;
      }

      for (int blockY = terrain::floorDiv(lowSample, blockSize); blockY <= terrain::floorDiv(highSample, blockSize);
           ++blockY)
      {
        const int slot = blockY + blockYOffset;

        if (slot >= 0 && slot < blockYCount)
        {
          crossed[(static_cast<size_t>(blockZ) * blocksPerSide + static_cast<size_t>(blockX)) * blockYCount +
                  static_cast<size_t>(slot)] = true;
        }
      }
    };

    for (int k = 0; k < columns; ++k)
    {
      for (int i = 0; i < columns; ++i)
      {
        const size_t at = static_cast<size_t>(k) * columns + static_cast<size_t>(i);
        const float reach = terrain::bandVoxels * voxelSize / grid.slope[at];
        const int low = static_cast<int>(std::floor((grid.height[at] - reach) / voxelSize)) + 1;
        const int high = static_cast<int>(std::ceil((grid.height[at] + reach) / voxelSize)) - 1;
        mark(i / blockSize, k / blockSize, low, high);
      }
    }

    return static_cast<size_t>(std::count(crossed.begin(), crossed.end(), true));
  }
}

TEST(TerrainMeasurement, RollingTerrain512Meters)
{
  const auto started = std::chrono::steady_clock::now();
  const Grid grid = buildGrid();
  terrain::TerrainVolume volume(voxelSize);
  constexpr int chunksPerSide = columns / terrain::chunkSize;
  // One full chunk layer below the lowest band is held as solid; nothing lies beneath it.
  const int bottomLayer = terrain::floorDiv(lowestBandSample(grid), terrain::chunkSize) - 1;

  for (int chunkZ = 0; chunkZ < chunksPerSide; ++chunkZ)
  {
    for (int chunkX = 0; chunkX < chunksPerSide; ++chunkX)
    {
      const ColumnRange range = rangeOfColumn(grid, chunkX, chunkZ);
      const float reach = terrain::bandVoxels * voxelSize / range.smallestSlope;

      for (int chunkY = bottomLayer; chunkY <= 1; ++chunkY)
      {
        const float lowestUnits = static_cast<float>(chunkY * terrain::chunkSize) * voxelSize;
        const float highestUnits = lowestUnits + static_cast<float>(terrain::chunkSize - 1) * voxelSize;
        const bool aboveSurface = lowestUnits - range.highestHeight >= reach;
        const bool deepSolid = highestUnits - range.lowestHeight <= -reach;

        // The pruned cases are exactly what building the arrays would collapse to.
        if (aboveSurface)
        {
          continue;
        }

        if (deepSolid)
        {
          volume.setChunk({ chunkX, chunkY, chunkZ }, terrain::TerrainChunk::uniform(-terrain::airDistance, 0));
          continue;
        }

        volume.setChunk({ chunkX, chunkY, chunkZ }, buildChunk(grid, chunkX, chunkY, chunkZ));
      }
    }
  }

  const double fillSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

  size_t denseCount = 0;
  size_t uniformCount = 0;
  size_t encodedBytes = 0;

  for (const auto& [coord, chunk] : volume.chunks())
  {
    (chunk->isUniform() ? uniformCount : denseCount) += 1;
    encodedBytes += terrain::encodeChunk(*chunk).size();
  }

  const double volumeMiB = static_cast<double>(volume.byteSize()) / bytesPerMiB;
  const double encodedMiB = static_cast<double>(encodedBytes) / bytesPerMiB;
  const size_t denseSixteen = denseBlocksOfSixteen(grid);
  const double denseThirtyTwoMiB = static_cast<double>(denseCount) * 96.0 / 1024.0;
  const double denseSixteenMiB =
    static_cast<double>(denseSixteen) * 16.0 * 16.0 * 16.0 * 3.0 / bytesPerMiB;

  std::cout << "[terrain] chunk 32: dense " << denseCount << ", uniform " << uniformCount << ", byteSize "
            << volumeMiB << " MiB, encoded " << encodedMiB << " MiB, fill " << fillSeconds << " s, dense only " << denseThirtyTwoMiB << " MiB, bottom layer " << bottomLayer << "\n";
  std::cout << "[terrain] chunk 16 (analytic): dense " << denseSixteen << ", " << denseSixteenMiB << " MiB\n";

  RecordProperty("denseChunks32", std::to_string(denseCount));
  RecordProperty("uniformChunks32", std::to_string(uniformCount));
  RecordProperty("byteSizeMiB", std::to_string(volumeMiB));
  RecordProperty("encodedMiB", std::to_string(encodedMiB));
  RecordProperty("fillSeconds", std::to_string(fillSeconds));
  RecordProperty("denseOnlyMiB32", std::to_string(denseThirtyTwoMiB));
  RecordProperty("denseBlocks16", std::to_string(denseSixteen));
  RecordProperty("denseBlocks16MiB", std::to_string(denseSixteenMiB));

  EXPECT_GT(denseCount, 0u);
  EXPECT_GT(uniformCount, 0u);
  EXPECT_LT(volume.byteSize(), size_t{ 1 } << 30);
  EXPECT_GT(denseSixteen, 0u);
}
