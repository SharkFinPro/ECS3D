#include <gtest/gtest.h>

#include "terrain/TerrainChunk.h"
#include "terrain/TerrainRaycast.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace {
  using Field = std::function<float(glm::vec3)>;

  constexpr float hitTolerance = 5.0e-3f;

  void fillChunks(terrain::TerrainVolume& volume, const Field& signedDistanceUnits,
                  const std::span<const terrain::ChunkCoord> coords)
  {
    const float voxelSize = volume.voxelSize();

    for (const terrain::ChunkCoord coord : coords)
    {
      std::vector<int16_t> distances(terrain::samplesPerChunk);
      std::vector<uint8_t> materials(terrain::samplesPerChunk, 1);
      const glm::ivec3 origin = terrain::chunkOrigin(coord);

      for (int z = 0; z < terrain::chunkSize; ++z)
      {
        for (int y = 0; y < terrain::chunkSize; ++y)
        {
          for (int x = 0; x < terrain::chunkSize; ++x)
          {
            const glm::vec3 position = voxelSize * glm::vec3(origin + glm::ivec3(x, y, z));
            distances[static_cast<size_t>(terrain::sampleIndex(x, y, z))] =
              terrain::encodeDistance(signedDistanceUnits(position) / voxelSize);
          }
        }
      }

      volume.setChunk(coord, terrain::TerrainChunk::dense(std::move(distances), std::move(materials)));
    }
  }

  // Samples cover [-64, 64) on every axis, so chunk coordinates -2..1 including negative ones.
  void fillRegion(terrain::TerrainVolume& volume, const Field& signedDistanceUnits)
  {
    const std::vector<terrain::ChunkCoord> coords = terrain::chunksTouched({ { -64, -64, -64 }, { 64, 64, 64 } });
    fillChunks(volume, signedDistanceUnits, coords);
  }

  Field plane(const float height)
  {
    return [height](const glm::vec3 p) { return p.y - height; };
  }

  Field sphere(const glm::vec3 center, const float radius)
  {
    return [center, radius](const glm::vec3 p) { return glm::length(p - center) - radius; };
  }
}

TEST(TerrainRaycastTest, DownwardRayHitsPlane)
{
  terrain::TerrainVolume volume;
  fillRegion(volume, plane(10.0f));

  const auto hit = terrain::raycast(volume, { 3.0f, 40.0f, -7.0f }, { 0.0f, -1.0f, 0.0f }, 100.0f);

  ASSERT_TRUE(hit.has_value());
  EXPECT_NEAR(hit->point.y, 10.0f, hitTolerance);
  EXPECT_NEAR(hit->point.x, 3.0f, hitTolerance);
  EXPECT_NEAR(hit->point.z, -7.0f, hitTolerance);
  EXPECT_NEAR(hit->distance, 30.0f, hitTolerance);
  EXPECT_NEAR(hit->normal.x, 0.0f, 1.0e-3f);
  EXPECT_NEAR(hit->normal.y, 1.0f, 1.0e-3f);
  EXPECT_NEAR(hit->normal.z, 0.0f, 1.0e-3f);
}

TEST(TerrainRaycastTest, ObliqueRayHitsPlaneAtAnalyticPoint)
{
  terrain::TerrainVolume volume;
  fillRegion(volume, plane(10.0f));
  const glm::vec3 direction(1.0f, -1.0f, 0.5f);

  const auto hit = terrain::raycast(volume, { 0.0f, 30.0f, 0.0f }, direction, 200.0f);

  ASSERT_TRUE(hit.has_value());
  EXPECT_NEAR(hit->point.x, 20.0f, hitTolerance);
  EXPECT_NEAR(hit->point.y, 10.0f, hitTolerance);
  EXPECT_NEAR(hit->point.z, 10.0f, hitTolerance);
  EXPECT_NEAR(hit->distance, 20.0f * glm::length(direction), hitTolerance);
}

TEST(TerrainRaycastTest, RaysHitSphereFromSeveralDirections)
{
  const glm::vec3 center(5.0f, 12.0f, -3.0f);
  terrain::TerrainVolume volume;
  fillRegion(volume, sphere(center, 12.0f));

  const std::vector<std::pair<glm::vec3, float>> rays = {
    { { 1.0f, 0.0f, 0.0f }, 50.0f },    { { 0.0f, 1.0f, 0.0f }, 50.0f },    { { 0.0f, 0.0f, -1.0f }, 50.0f },
    { { 1.0f, 1.0f, 1.0f }, 50.0f },    { { -0.3f, 0.5f, -0.8f }, 50.0f },  { { 0.3f, 0.5f, 0.8f }, 1000.0f },
  };

  for (const auto& [towardOrigin, away] : rays)
  {
    const glm::vec3 outward = glm::normalize(towardOrigin);
    const auto hit = terrain::raycast(volume, center + outward * away, -outward, 2.0f * away);

    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(glm::length(hit->point - center), 12.0f, 0.15f);
    EXPECT_NEAR(hit->distance, away - 12.0f, 0.15f);
    EXPECT_GT(glm::dot(hit->normal, glm::normalize(hit->point - center)), 0.99f);
  }
}

TEST(TerrainRaycastTest, RayPointingAwayMisses)
{
  terrain::TerrainVolume volume;
  fillRegion(volume, plane(10.0f));
  const glm::vec3 origin(3.0f, 40.0f, -7.0f);

  EXPECT_TRUE(terrain::raycast(volume, origin, { 0.0f, -1.0f, 0.0f }, 100.0f).has_value());
  EXPECT_FALSE(terrain::raycast(volume, origin, { 0.0f, 1.0f, 0.0f }, 100.0f).has_value());
}

TEST(TerrainRaycastTest, MaxDistanceLimitsTheHit)
{
  terrain::TerrainVolume volume;
  fillRegion(volume, plane(10.0f));
  const glm::vec3 origin(3.0f, 40.0f, -7.0f);
  const glm::vec3 down(0.0f, -1.0f, 0.0f);

  EXPECT_FALSE(terrain::raycast(volume, origin, down, 29.9f).has_value());

  const auto hit = terrain::raycast(volume, origin, down, 30.1f);
  ASSERT_TRUE(hit.has_value());
  EXPECT_NEAR(hit->distance, 30.0f, hitTolerance);
}

TEST(TerrainRaycastTest, RayStartingInsideHitsAtOrigin)
{
  terrain::TerrainVolume volume;
  fillRegion(volume, plane(10.0f));
  const glm::vec3 inside(3.0f, 0.0f, -7.0f);

  const auto hit = terrain::raycast(volume, inside, { 0.0f, 0.0f, 1.0f }, 50.0f);

  ASSERT_TRUE(hit.has_value());
  EXPECT_FLOAT_EQ(hit->distance, 0.0f);
  EXPECT_FLOAT_EQ(hit->point.x, inside.x);
  EXPECT_FLOAT_EQ(hit->point.y, inside.y);
  EXPECT_FLOAT_EQ(hit->point.z, inside.z);
  EXPECT_GT(hit->normal.y, 0.99f);

  const auto outsideHit = terrain::raycast(volume, { 3.0f, 20.0f, -7.0f }, { 0.0f, -1.0f, 0.0f }, 50.0f);
  ASSERT_TRUE(outsideHit.has_value());
  EXPECT_GT(outsideHit->distance, 1.0f);
}

TEST(TerrainRaycastTest, EmptyVolumeFindsNothing)
{
  const terrain::TerrainVolume empty;
  terrain::TerrainVolume filled;
  fillRegion(filled, plane(10.0f));
  const glm::vec3 origin(3.0f, 40.0f, -7.0f);
  const glm::vec3 down(0.0f, -1.0f, 0.0f);

  EXPECT_FALSE(terrain::raycast(empty, origin, down, 100.0f).has_value());
  EXPECT_TRUE(terrain::raycast(filled, origin, down, 100.0f).has_value());
}

TEST(TerrainRaycastTest, HugeMaxDistanceOnSparseVolumeIsCorrect)
{
  const glm::vec3 center(0.0f);
  terrain::TerrainVolume volume;
  fillRegion(volume, sphere(center, 12.0f));
  const float huge = 1.0e9f;

  const auto hit = terrain::raycast(volume, { 0.0f, 5000.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, huge);
  const auto farMiss = terrain::raycast(volume, { 500.0f, 5000.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, huge);
  const auto skimMiss = terrain::raycast(volume, { -500.0f, 30.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, huge);
  const auto skimHit = terrain::raycast(volume, { -500.0f, 5.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, huge);

  ASSERT_TRUE(hit.has_value());
  EXPECT_NEAR(hit->point.y, 12.0f, 0.15f);
  EXPECT_NEAR(hit->distance, 5000.0f - 12.0f, 0.2f);
  EXPECT_FALSE(farMiss.has_value());
  EXPECT_FALSE(skimMiss.has_value());
  ASSERT_TRUE(skimHit.has_value());
  EXPECT_LT(skimHit->point.x, 0.0f);
}

TEST(TerrainRaycastTest, SurfaceInLastSampleLayerBesideAbsentChunkIsHit)
{
  // The slab straddles the border between chunk x=0 and x=1, but chunk (0, 0, 0) is never set, so its last sample
  // layer reads as air while the cells in it still touch the solid samples of chunk (1, 0, 0). A ball in chunk
  // (-1, 0, 0) widens the occupied bounds so the ray reaches the absent chunk before the clip would start it.
  const float slabCenter = 78.75f;
  const float halfThickness = 2.0f;
  const Field slabAndBall = [=](const glm::vec3 p) {
    const float slab = std::abs(p.x - slabCenter) - halfThickness;
    const float ball = glm::length(p - glm::vec3(-40.0f, 60.0f, 60.0f)) - 5.0f;
    return std::min(slab, ball);
  };

  terrain::TerrainVolume volume;
  const std::vector<terrain::ChunkCoord> coords = { { -1, 0, 0 }, { 1, 0, 0 } };
  fillChunks(volume, slabAndBall, coords);
  ASSERT_EQ(volume.chunks().size(), 2u);
  ASSERT_EQ(volume.chunk({ 0, 0, 0 }), nullptr);

  const auto hit = terrain::raycast(volume, { -100.0f, 20.0f, 20.0f }, { 1.0f, 0.0f, 0.0f }, 500.0f);

  ASSERT_TRUE(hit.has_value());
  // Sample 31 reads as air (+4 voxels) and sample 32 is -0.3 voxels, so the trilinear zero sits at 4 / 4.3 of the cell.
  EXPECT_NEAR(hit->point.x, 77.5f + 2.5f * (4.0f / 4.3f), 0.05f);
  EXPECT_LT(hit->normal.x, -0.99f);
}

namespace {
  struct LastLayerCase
  {
    int rayAxis;
    float raySign;
    int layerAxis;
  };

  glm::vec3 withAxes(const int firstAxis, const float first, const int secondAxis, const float second,
                     const int thirdAxis, const float third)
  {
    glm::vec3 value(0.0f);
    value[firstAxis] = first;
    value[secondAxis] = second;
    value[thirdAxis] = third;
    return value;
  }
}

class TerrainRaycastLastLayerTest : public ::testing::TestWithParam<LastLayerCase> {};

TEST_P(TerrainRaycastLastLayerTest, SurfaceReachedAlongAbsentChunkLastLayerIsHit)
{
  // The ray runs inside absent chunk (0, 0, 0) along its last sample layer of layerAxis, where every cell reaches into
  // the solid box of the neighbor chunk one step along layerAxis. Skipping any cell of that layer would miss it.
  const auto [rayAxis, raySign, layerAxis] = GetParam();
  const int otherAxis = 3 - rayAxis - layerAxis;
  const glm::vec3 boxCenter = withAxes(layerAxis, 43.0f, rayAxis, 10.0f, otherAxis, 15.0f);
  const glm::vec3 boxHalf = withAxes(layerAxis, 17.0f, rayAxis, 5.0f, otherAxis, 10.0f);
  const Field box = [=](const glm::vec3 p) {
    const glm::vec3 d = glm::abs(p - boxCenter) - boxHalf;
    return glm::length(glm::max(d, glm::vec3(0.0f))) + std::min(std::max(d.x, std::max(d.y, d.z)), 0.0f);
  };

  glm::ivec3 offset(0);
  offset[layerAxis] = 1;
  const terrain::ChunkCoord neighbor{ offset.x, offset.y, offset.z };
  terrain::TerrainVolume volume(1.0f);
  const std::vector<terrain::ChunkCoord> coords = { neighbor };
  fillChunks(volume, box, coords);
  ASSERT_EQ(volume.chunk({ 0, 0, 0 }), nullptr);
  ASSERT_NE(volume.chunk(neighbor), nullptr);

  const float startAlong = raySign > 0.0f ? 0.5f : 25.5f;
  const glm::vec3 origin = withAxes(rayAxis, startAlong, layerAxis, 31.5f, otherAxis, 15.0f);
  const glm::vec3 direction = withAxes(rayAxis, raySign, layerAxis, 0.0f, otherAxis, 0.0f);

  const auto hit = terrain::raycast(volume, origin, direction, 100.0f);

  ASSERT_TRUE(hit.has_value());
  // Halfway into the cell the trilinear value is (4 + depth of the box sample) / 2, which crosses zero 4 voxels deep.
  const float expectedAlong = raySign > 0.0f ? 9.0f : 11.0f;
  EXPECT_NEAR(hit->point[rayAxis], expectedAlong, 0.05f);
  EXPECT_NEAR(hit->point[layerAxis], 31.5f, 1.0e-3f);
  EXPECT_LT(hit->normal[layerAxis], -0.99f);
}

INSTANTIATE_TEST_SUITE_P(TerrainRaycastAxes, TerrainRaycastLastLayerTest,
                         ::testing::Values(LastLayerCase{ 0, 1.0f, 1 }, LastLayerCase{ 0, -1.0f, 2 },
                                           LastLayerCase{ 1, 1.0f, 2 }, LastLayerCase{ 1, -1.0f, 0 },
                                           LastLayerCase{ 2, 1.0f, 0 }, LastLayerCase{ 2, -1.0f, 1 }));

TEST(TerrainRaycastTest, ExtremeOriginFindsNothingWithoutOverflow)
{
  terrain::TerrainVolume volume;
  fillRegion(volume, plane(10.0f));

  EXPECT_TRUE(terrain::raycast(volume, { 3.0f, 40.0f, -7.0f }, { 0.0f, -1.0f, 0.0f }, 100.0f).has_value());
  EXPECT_FALSE(terrain::raycast(volume, { 1.0e30f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, 1.0e31f).has_value());
  EXPECT_FALSE(terrain::raycast(volume, { 0.0f, -1.0e30f, 0.0f }, { 0.0f, 1.0f, 0.0f }, 1.0e31f).has_value());
}

TEST(TerrainRaycastTest, InvalidInputsFindNothing)
{
  terrain::TerrainVolume volume;
  fillRegion(volume, plane(10.0f));
  const glm::vec3 origin(3.0f, 40.0f, -7.0f);
  const glm::vec3 down(0.0f, -1.0f, 0.0f);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float infinity = std::numeric_limits<float>::infinity();

  EXPECT_TRUE(terrain::raycast(volume, origin, down, 100.0f).has_value());
  EXPECT_FALSE(terrain::raycast(volume, { nan, 40.0f, -7.0f }, down, 100.0f).has_value());
  EXPECT_FALSE(terrain::raycast(volume, origin, { nan, -1.0f, 0.0f }, 100.0f).has_value());
  EXPECT_FALSE(terrain::raycast(volume, origin, { 0.0f, 0.0f, 0.0f }, 100.0f).has_value());
  EXPECT_FALSE(terrain::raycast(volume, origin, down, -1.0f).has_value());
  EXPECT_FALSE(terrain::raycast(volume, origin, down, nan).has_value());
  EXPECT_FALSE(terrain::raycast(volume, origin, down, infinity).has_value());
}
