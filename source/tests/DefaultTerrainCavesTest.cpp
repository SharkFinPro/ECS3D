#include <gtest/gtest.h>

#include "DefaultTerrainCaves.h"
#include "DefaultTerrainPaint.h"
#include "DefaultTerrainTiles.h"
#include "DefaultTerrainTilesFixture.h"
#include "terrain/TerrainRaycast.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <glm/vec3.hpp>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

using basintest::DefaultTerrainTilesTest;
using basintest::tileIndex;

namespace {
  constexpr float voxel = basin::voxelSize;

  // The tiles the caves lie in.
  constexpr int northRow = 2;
  constexpr int northCol = 1;
  constexpr int eastRow = 1;
  constexpr int eastCol = 2;

  // Rays into one tile, written in global sample columns and world heights.
  struct Probe
  {
    const terrain::TerrainVolume& volume;
    int row;
    int col;

    [[nodiscard]] glm::vec3 at(const float columnX, const float worldY, const float columnZ) const
    {
      return { (columnX - static_cast<float>(col * basin::tileStride)) * voxel,
               worldY - basin::tileOrigin(row, col).y,
               (columnZ - static_cast<float>(row * basin::tileStride)) * voxel };
    }

    [[nodiscard]] std::optional<terrain::RayHit> cast(const glm::vec3& origin, const glm::vec3& direction,
                                                      const float maxDistance) const
    {
      return terrain::raycast(volume, origin, direction, maxDistance);
    }

    // The world height of the first rock a ray straight down from the sky meets.
    [[nodiscard]] std::optional<float> skyDown(const float columnX, const float columnZ) const
    {
      const auto hit = cast(at(columnX, 200.0f, columnZ), { 0.0f, -1.0f, 0.0f }, 400.0f);

      return hit ? std::optional<float>(hit->point.y + basin::tileOrigin(row, col).y) : std::nullopt;
    }
  };

  float columnOf(const float world, const bool xAxis)
  {
    const glm::vec3 origin = basin::tileOrigin(0, 0);

    return (world - (xAxis ? origin.x : origin.z)) / voxel;
  }

  const basin::caves::Stack& stackNamed(const std::string& name)
  {
    for (const auto& stack : basin::caves::stacks())
    {
      if (name == stack.name)
      {
        return stack;
      }
    }

    throw std::runtime_error("no stack named " + name);
  }

  Probe probeFor(const basintest::Tiles& tiles, const int row, const int col)
  {
    return { *tiles[tileIndex(row, col)], row, col };
  }

  // The tile a stack lies in.
  Probe probeForStack(const basintest::Tiles& tiles, const basin::caves::Stack& stack)
  {
    const int col = static_cast<int>(columnOf(stack.worldX, true)) / basin::tileStride;
    const int row = static_cast<int>(columnOf(stack.worldZ, false)) / basin::tileStride;

    return probeFor(tiles, row, col);
  }
}

TEST_F(DefaultTerrainTilesTest, EveryEnclosedStackStandsOnAFlatFloorUnderRockWithRoomForItsBodies)
{
  int enclosed = 0;

  for (const auto& stack : basin::caves::stacks())
  {
    if (!stack.enclosed)
    {
      continue;
    }

    SCOPED_TRACE(stack.name);
    ++enclosed;
    const auto probe = probeForStack(s_tiles, stack);
    const float columnX = columnOf(stack.worldX, true);
    const float columnZ = columnOf(stack.worldZ, false);
    const float span = basin::caves::stackHalfSpan(stack) / voxel;

    // Under the stack, in its middle and at its four corners, the floor is where the scene says it is.
    for (const auto& [dx, dz] : { std::pair{ 0.0f, 0.0f }, { span, span }, { -span, span }, { span, -span },
                                  { -span, -span } })
    {
      const auto floor = probe.cast(probe.at(columnX + dx, stack.surfaceY + 6.0f, columnZ + dz), { 0, -1, 0 }, 30.0f);
      ASSERT_TRUE(floor.has_value()) << dx << "," << dz;
      EXPECT_NEAR(floor->point.y + basin::tileOrigin(probe.row, probe.col).y, stack.surfaceY, 0.6f);

      // The roof is solid and well above the highest layer of bodies.
      const float stackTop = stack.surfaceY + basin::caves::stackLift +
                             static_cast<float>(stack.layers - 1) * basin::stackSpacing + 2.0f;
      const auto roof = probe.cast(probe.at(columnX + dx, stack.surfaceY + 6.0f, columnZ + dz), { 0, 1, 0 }, 80.0f);
      ASSERT_TRUE(roof.has_value()) << dx << "," << dz;
      EXPECT_GT(roof->point.y + basin::tileOrigin(probe.row, probe.col).y, stackTop + 1.0f);

      // Positive control: from the sky the same column's first rock is far above the floor, so the floor really is
      // under a roof rather than on the surface.
      const auto sky = probe.skyDown(columnX + dx, columnZ + dz);
      ASSERT_TRUE(sky.has_value());
      EXPECT_GT(*sky, stack.surfaceY + 15.0f);
    }
  }

  EXPECT_EQ(enclosed, 3);
}

TEST_F(DefaultTerrainTilesTest, TheArchTopIsFlatUnderItsStackAndHigherThanTheTerrainAroundIt)
{
  const auto& top = stackNamed("Arch top");
  const auto probe = probeForStack(s_tiles, top);
  const float columnX = columnOf(top.worldX, true);
  const float columnZ = columnOf(top.worldZ, false);
  const float span = basin::caves::stackHalfSpan(top) / voxel;

  for (const auto& [dx, dz] : { std::pair{ 0.0f, 0.0f }, { span, span }, { -span, span }, { span, -span },
                                { -span, -span } })
  {
    const auto hit = probe.skyDown(columnX + dx, columnZ + dz);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(*hit, top.surfaceY, 1.6f) << dx << "," << dz;
  }

  // Positive control: beside the arch the arena floor is at zero, so the top really stands above it.
  const auto beside = probe.skyDown(columnX, columnZ + 45.0f);
  ASSERT_TRUE(beside.has_value());
  EXPECT_NEAR(*beside, 0.0f, 0.6f);
  EXPECT_GT(top.surfaceY, 30.0f);
}

TEST_F(DefaultTerrainTilesTest, TheArenaFloorRunsThroughTheArchPassage)
{
  const auto& passage = stackNamed("Arch passage");
  const auto probe = probeForStack(s_tiles, passage);
  const float columnX = columnOf(passage.worldX, true);
  const float columnZ = columnOf(passage.worldZ, false);

  // A ray along x through the passage meets nothing across the arch's whole width and beyond it.
  const glm::vec3 along(1.0f, 0.0f, 0.0f);
  const auto clear = probe.cast(probe.at(columnX - 25.0f, 3.0f, columnZ), along, 50.0f * voxel);
  EXPECT_FALSE(clear.has_value());

  // Positive control: at the deck's height the same ray meets the arch, where the passage's crown is.
  const auto deck = probe.cast(probe.at(columnX - 25.0f, 28.0f, columnZ), along, 50.0f * voxel);
  ASSERT_TRUE(deck.has_value());
  EXPECT_NEAR(deck->distance, 12.0f * voxel, 3.0f * voxel);

  // The floor under the passage is the arena floor.
  const auto floor = probe.skyDown(columnX, columnZ - 10.0f);
  ASSERT_TRUE(floor.has_value());
  EXPECT_GT(*floor, 30.0f) << "the sky ray lands on the deck";
  const auto under = probe.cast(probe.at(columnX, 6.0f, columnZ - 10.0f), { 0, -1, 0 }, 30.0f);
  ASSERT_TRUE(under.has_value());
  EXPECT_NEAR(under->point.y + basin::tileOrigin(probe.row, probe.col).y, 0.0f, 0.6f);
}

TEST_F(DefaultTerrainTilesTest, TheNorthTunnelLeadsFromTheArenaFloorIntoItsChamber)
{
  const auto& north = stackNamed("North cave");
  const auto probe = probeFor(s_tiles, northRow, northCol);
  const float columnX = columnOf(north.worldX, true);

  // Down the tunnel's axis, at body height above the floor, a ray meets no rock until the chamber's far wall.
  const glm::vec3 along(0.0f, 0.0f, 1.0f);
  const auto wall = probe.cast(probe.at(columnX, 6.0f, 262.0f), along, 200.0f * voxel);
  ASSERT_TRUE(wall.has_value());
  EXPECT_GT(wall->distance, 75.0f * voxel);
  EXPECT_LT(wall->distance, 100.0f * voxel);

  // Positive control: just beside the tunnel the same ray meets the rim's slope long before that.
  const auto beside = probe.cast(probe.at(columnX + 14.0f, 6.0f, 262.0f), along, 200.0f * voxel);
  ASSERT_TRUE(beside.has_value());
  EXPECT_LT(beside->distance, 50.0f * voxel);

  // The mouth: where the rim is still low the tunnel is a cut, floored at the arena's level and open to the sky.
  const auto cut = probe.skyDown(columnX, 292.0f);
  ASSERT_TRUE(cut.has_value());
  EXPECT_NEAR(*cut, 0.0f, 0.6f);
}

TEST_F(DefaultTerrainTilesTest, TheEastCavernOpensToTheArenaAndThroughAShaftToTheSky)
{
  const auto& east = stackNamed("East cavern");
  const auto probe = probeFor(s_tiles, eastRow, eastCol);
  const float columnZ = columnOf(east.worldZ, false);

  // Through the tunnel from the arena into the cavern, a ray meets rock only at the chamber's far wall.
  const glm::vec3 along(1.0f, 0.0f, 0.0f);
  const auto wall = probe.cast(probe.at(270.0f, 6.0f, columnZ), along, 200.0f * voxel);
  ASSERT_TRUE(wall.has_value());
  EXPECT_GT(wall->distance, 70.0f * voxel);
  EXPECT_LT(wall->distance, 90.0f * voxel);

  // The shaft: a ray from the sky reaches the cavern's floor, while one beside it lands on the plateau.
  // It lands on the ramp, four voxels (six units) above the cavern's level floor.
  const auto shaft = probe.skyDown(346.0f, 200.0f);
  ASSERT_TRUE(shaft.has_value());
  EXPECT_NEAR(*shaft, 6.0f, 1.0f);

  const auto plateau = probe.skyDown(346.0f + 20.0f, 200.0f);
  ASSERT_TRUE(plateau.has_value());
  EXPECT_GT(*plateau, 60.0f);
}

TEST_F(DefaultTerrainTilesTest, TheFeaturesGiveTheArchAndTheCavesRockMaterials)
{
  const auto& top = stackNamed("Arch top");
  const auto probe = probeForStack(s_tiles, top);
  const int columnX = static_cast<int>(columnOf(top.worldX, true));
  const int columnZ = static_cast<int>(columnOf(top.worldZ, false));
  const int topSample = basin::groundHeight + static_cast<int>(std::lround(top.surfaceY / voxel));
  const auto& volume = probe.volume;
  const glm::ivec3 local(columnX - probe.col * basin::tileStride, topSample - 1,
                         columnZ - probe.row * basin::tileStride);

  EXPECT_EQ(volume.materialAtSample(local), defaultterrain::materialGray);

  // Positive control: the arena floor beside the arch is not rock.
  const glm::ivec3 floor(local.x, basin::groundHeight - 1, local.z + 45);
  EXPECT_NE(volume.materialAtSample(floor), defaultterrain::materialGray);
}
