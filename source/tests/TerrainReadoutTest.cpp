#include "TerrainReadout.h"
#include <gtest/gtest.h>

namespace
{
  terrain::TerrainSurfaceStatus surface(const float pending, const bool growing = false)
  {
    terrain::TerrainSurfaceStatus status;
    status.hasSurface = true;
    status.pending = pending;
    status.growing = growing;
    return status;
  }
}

TEST(TerrainReadout, ChunksShowHeldAgainstExpected)
{
  EXPECT_EQ(terrain::terrainChunksText(512, 512), "512 / 512");
  EXPECT_EQ(terrain::terrainChunksText(37, 512), "37 / 512");
  EXPECT_EQ(terrain::terrainChunksText(0, 512), "0 / 512");
}

TEST(TerrainReadout, ChunksShowOnlyHeldWhenNothingIsAnnounced)
{
  EXPECT_EQ(terrain::terrainChunksText(37, 0), "37");
  EXPECT_EQ(terrain::terrainChunksText(0, 0), "0");
}

TEST(TerrainReadout, ChunksShowDashWithoutAVolume)
{
  EXPECT_EQ(terrain::terrainChunksText(std::nullopt, 512), "-");
  EXPECT_EQ(terrain::terrainChunksText(std::nullopt, 0), "-");
}

TEST(TerrainReadout, SurfaceWithoutOneShowsNone)
{
  EXPECT_EQ(terrain::terrainSurfaceText({}), "none");

  terrain::TerrainSurfaceStatus status;
  status.pending = 0.5f;
  EXPECT_EQ(terrain::terrainSurfaceText(status), "none");
}

TEST(TerrainReadout, SurfaceWithNothingPendingIsReady)
{
  EXPECT_EQ(terrain::terrainSurfaceText(surface(0.0f)), "ready");
}

TEST(TerrainReadout, SurfaceShowsRoundedPercent)
{
  EXPECT_EQ(terrain::terrainSurfaceText(surface(0.5f)), "50% pending");
  EXPECT_EQ(terrain::terrainSurfaceText(surface(0.37f)), "37% pending");
  EXPECT_EQ(terrain::terrainSurfaceText(surface(1.0f)), "100% pending");
}

TEST(TerrainReadout, NonzeroPendingNeverShowsZeroPercent)
{
  EXPECT_EQ(terrain::terrainSurfaceText(surface(0.004f)), "1% pending");
}

TEST(TerrainReadout, PendingBelowOneNeverShowsHundredPercent)
{
  EXPECT_EQ(terrain::terrainSurfaceText(surface(0.996f)), "99% pending");
}

TEST(TerrainReadout, OutOfRangePendingIsClamped)
{
  EXPECT_EQ(terrain::terrainSurfaceText(surface(1.7f)), "100% pending");
  EXPECT_EQ(terrain::terrainSurfaceText(surface(-0.3f)), "ready");
}

TEST(TerrainReadout, GrowingShowsTheReplacementsProgress)
{
  EXPECT_EQ(terrain::terrainSurfaceText(surface(0.12f, true)), "growing, 12% pending");
  EXPECT_EQ(terrain::terrainSurfaceText(surface(0.004f, true)), "growing, 1% pending");
}

TEST(TerrainReadout, TooLargeTakesPrecedenceOverEverything)
{
  terrain::TerrainSurfaceStatus status = surface(0.5f, true);
  status.tooLarge = true;
  EXPECT_EQ(terrain::terrainSurfaceText(status), "too large to draw");

  status.hasSurface = false;
  EXPECT_EQ(terrain::terrainSurfaceText(status), "too large to draw");
}
