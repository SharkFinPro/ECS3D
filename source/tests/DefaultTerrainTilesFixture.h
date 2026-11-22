#ifndef DEFAULTTERRAINTILESFIXTURE_H
#define DEFAULTTERRAINTILESFIXTURE_H

#include <gtest/gtest.h>

#include "DefaultTerrainTiles.h"
#include "ProcessCpuClock.h"
#include "assets/AssetRegistry.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainVolume.h"

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <uuid.h>

// Shared by the Terrain Basin suites: the nine tiles, generated once per run, and the seams between them.
namespace basintest {
  using Tiles = std::array<std::shared_ptr<terrain::TerrainVolume>, basin::tileCount>;

  inline constexpr int lastColumn = basin::tileColumns - 1;

  // Columns two neighbors hold in common: the seam and one apron column on each side. A's local column tileStride + k is
  // B's local column k.
  inline constexpr int overlapColumns = basin::tileColumns - basin::tileStride;

  inline size_t tileIndex(const int row, const int col)
  {
    return static_cast<size_t>(row * basin::tilesPerSide + col);
  }

  inline AssetRecord terrainRecord(const std::string& uuid)
  {
    const auto parsed = uuids::uuid::from_string(uuid).value();

    return { .uuid = parsed, .type = AssetType::Terrain, .path = terrainAssetPath(parsed) };
  }

  inline bool sameChunks(const terrain::TerrainVolume& a, const terrain::TerrainVolume& b)
  {
    if (a.chunks().size() != b.chunks().size())
    {
      return false;
    }

    for (const auto& [coord, chunk] : a.chunks())
    {
      const auto other = b.chunk(coord);
      if (!other || other->hash() != chunk->hash())
      {
        return false;
      }
    }

    return true;
  }

  // Two neighboring tiles. For an east pair, a is west of b and they overlap on a's last overlapColumns columns and
  // b's first; for a north pair, a is south of b and they overlap on a's last rows and b's first.
  struct Seam
  {
    int rowA;
    int colA;
    int rowB;
    int colB;
    bool east;

    [[nodiscard]] size_t a() const
    {
      return tileIndex(rowA, colA);
    }

    [[nodiscard]] size_t b() const
    {
      return tileIndex(rowB, colB);
    }

    [[nodiscard]] std::string label() const
    {
      return "tile " + std::to_string(a()) + (east ? " west of " : " south of ") + std::to_string(b());
    }
  };

  // Every seam of the 3 x 3 grid: twelve of them.
  inline void forEachSeam(const std::function<void(const Seam&)>& visit)
  {
    for (int row = 0; row < basin::tilesPerSide; ++row)
    {
      for (int col = 0; col < basin::tilesPerSide; ++col)
      {
        if (col + 1 < basin::tilesPerSide)
        {
          visit({ row, col, row, col + 1, true });
        }

        if (row + 1 < basin::tilesPerSide)
        {
          visit({ row, col, row + 1, col, false });
        }
      }
    }
  }

  class DefaultTerrainTilesTest : public testing::Test
  {
  protected:
    static void SetUpTestSuite()
    {
      std::array<terrain::TerrainVolume*, basin::tileCount> pointers{};
      for (size_t index = 0; index < s_tiles.size(); ++index)
      {
        s_tiles[index] = std::make_shared<terrain::TerrainVolume>(basin::voxelSize);
        pointers[index] = s_tiles[index].get();
      }

      const double start = testclock::processCpuSeconds();
      generateBasinTiles(pointers);
      s_seconds = testclock::processCpuSeconds() - start;
    }

    static void TearDownTestSuite()
    {
      for (auto& tile : s_tiles)
      {
        tile.reset();
      }
    }

    static size_t totalChunks()
    {
      size_t total = 0;
      for (const auto& tile : s_tiles)
      {
        total += tile->chunks().size();
      }

      return total;
    }

    inline static Tiles s_tiles;
    inline static double s_seconds = 0.0;
  };
}

#endif //DEFAULTTERRAINTILESFIXTURE_H
