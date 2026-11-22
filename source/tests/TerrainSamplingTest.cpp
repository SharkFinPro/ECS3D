#include <gtest/gtest.h>

#include "terrain/TerrainChunk.h"
#include "terrain/TerrainSampling.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <random>
#include <utility>
#include <vector>

namespace {
  using Field = std::function<float(glm::vec3)>;

  constexpr float quantizationTolerance = 2.0e-4f;

  // Samples cover [-64, 64) on every axis, so chunk coordinates -2..1 including negative ones.
  void fillField(terrain::TerrainVolume& volume, const Field& signedDistanceUnits)
  {
    const float voxelSize = volume.voxelSize();

    for (const terrain::ChunkCoord coord : terrain::chunksTouched({ { -64, -64, -64 }, { 64, 64, 64 } }))
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

  Field plane(const float height)
  {
    return [height](const glm::vec3 p) { return p.y - height; };
  }

  Field tiltedPlane(const glm::vec3 normal, const float offset)
  {
    return [normal, offset](const glm::vec3 p) { return glm::dot(p, normal) - offset; };
  }

  Field sphere(const glm::vec3 center, const float radius)
  {
    return [center, radius](const glm::vec3 p) { return glm::length(p - center) - radius; };
  }

  glm::vec3 randomPointNear(std::mt19937& rng, const float extent)
  {
    std::uniform_real_distribution<float> axis(-extent, extent);
    return { axis(rng), axis(rng), axis(rng) };
  }
}

TEST(TerrainSamplingTest, HorizontalPlaneDistanceAndGradient)
{
  terrain::TerrainVolume volume;
  fillField(volume, plane(7.3f));
  terrain::Sampler sampler(volume);
  std::mt19937 rng(7);
  std::uniform_real_distribution<float> offset(-5.0f, 5.0f);
  std::uniform_real_distribution<float> lateral(-100.0f, 100.0f);

  for (int i = 0; i < 200; ++i)
  {
    const glm::vec3 point(lateral(rng), 7.3f + offset(rng), lateral(rng));
    const glm::vec3 gradient = sampler.gradient(point);

    EXPECT_NEAR(sampler.distance(point), (point.y - 7.3f) / 2.5f, quantizationTolerance);
    EXPECT_NEAR(gradient.x, 0.0f, 1.0e-3f);
    EXPECT_NEAR(gradient.y, 1.0f / 2.5f, 1.0e-3f);
    EXPECT_NEAR(gradient.z, 0.0f, 1.0e-3f);
    EXPECT_NEAR(sampler.metricDistance(point), point.y - 7.3f, 1.0e-2f);
  }
}

TEST(TerrainSamplingTest, TiltedPlaneDirectionAndLength)
{
  const glm::vec3 normal = glm::normalize(glm::vec3(1.0f, 2.0f, 0.5f));
  terrain::TerrainVolume volume;
  fillField(volume, tiltedPlane(normal, 3.0f));
  terrain::Sampler sampler(volume);
  std::mt19937 rng(11);

  int checked = 0;

  for (int i = 0; i < 1000; ++i)
  {
    const glm::vec3 point = randomPointNear(rng, 100.0f);
    const float exactUnits = glm::dot(point, normal) - 3.0f;

    if (std::abs(exactUnits) > 5.0f)
    {
      continue;
    }

    ++checked;
    const glm::vec3 gradient = sampler.gradient(point);

    EXPECT_NEAR(sampler.distance(point), exactUnits / 2.5f, quantizationTolerance);
    EXPECT_NEAR(glm::length(gradient), 1.0f / 2.5f, 1.0e-3f);
    EXPECT_GT(glm::dot(glm::normalize(gradient), normal), 0.9999f);
    EXPECT_NEAR(sampler.metricDistance(point), exactUnits, 1.0e-2f);
  }

  EXPECT_GT(checked, 20);
}

TEST(TerrainSamplingTest, SphereDistanceAndRadialGradient)
{
  const glm::vec3 center(5.0f, -10.0f, 15.0f);
  constexpr float radius = 40.0f;
  terrain::TerrainVolume volume;
  fillField(volume, sphere(center, radius));
  terrain::Sampler sampler(volume);
  std::mt19937 rng(23);
  std::uniform_real_distribution<float> angle(-1.0f, 1.0f);
  std::uniform_real_distribution<float> shell(-2.0f, 2.0f);

  int checked = 0;

  for (int i = 0; i < 300; ++i)
  {
    const glm::vec3 direction(angle(rng), angle(rng), angle(rng));

    if (glm::length(direction) < 0.1f)
    {
      continue;
    }

    ++checked;
    const glm::vec3 unit = glm::normalize(direction);
    const float exactUnits = shell(rng);
    const glm::vec3 point = center + unit * (radius + exactUnits);
    const glm::vec3 gradient = sampler.gradient(point);

    EXPECT_NEAR(sampler.distance(point), exactUnits / 2.5f, 0.05f);
    EXPECT_GT(glm::dot(glm::normalize(gradient), unit), 0.99f);
    EXPECT_NEAR(glm::length(gradient), 1.0f / 2.5f, 0.06f / 2.5f);
    EXPECT_NEAR(sampler.metricDistance(point), exactUnits, 0.2f);
  }

  EXPECT_GT(checked, 100);
}

TEST(TerrainSamplingTest, ValuesAndGradientsAgreeEitherSideOfAChunkBorder)
{
  struct Case
  {
    glm::vec3 center;
    glm::vec3 probe;
    int axis;
  };

  // Chunk borders sit at sample 32 (80 units) and sample -32 (-80 units).
  for (const Case c : { Case{ { 80.0f, 0.0f, 0.0f }, { 80.0f, 30.0f, 0.0f }, 0 },
                        Case{ { -80.0f, 0.0f, 0.0f }, { -80.0f, 0.0f, 30.0f }, 0 },
                        Case{ { 0.0f, 80.0f, 0.0f }, { 0.0f, 80.0f, 30.0f }, 1 },
                        Case{ { 0.0f, 0.0f, 80.0f }, { 30.0f, 0.0f, 80.0f }, 2 },
                        Case{ { 0.0f, 0.0f, -80.0f }, { 0.0f, 30.0f, -80.0f }, 2 } })
  {
    terrain::TerrainVolume volume;
    fillField(volume, sphere(c.center, 30.0f));
    terrain::Sampler sampler(volume);

    const float axisStep = 1.0e-3f;
    glm::vec3 below = c.probe;
    glm::vec3 above = c.probe;
    below[c.axis] -= axisStep;
    above[c.axis] += axisStep;

    EXPECT_NEAR(sampler.distance(below), sampler.distance(above), 1.0e-3f);
    EXPECT_NEAR(sampler.distance(below), 0.0f, 0.05f);
    EXPECT_NEAR(glm::length(sampler.gradient(below) - sampler.gradient(above)), 0.0f, 0.05f);
    EXPECT_GT(glm::length(sampler.gradient(below)), 0.3f);
  }
}

TEST(TerrainSamplingTest, PlaneAcrossAChunkBorderIsSeamless)
{
  const glm::vec3 normal = glm::normalize(glm::vec3(0.3f, 1.0f, -0.2f));
  terrain::TerrainVolume volume;
  fillField(volume, tiltedPlane(normal, 0.0f));
  terrain::Sampler sampler(volume);

  for (const float border : { 80.0f, -80.0f, 0.0f })
  {
    const glm::vec3 below(border - 1.0e-3f, 1.0f, 3.0f);
    const glm::vec3 above(border + 1.0e-3f, 1.0f, 3.0f);

    EXPECT_NEAR(sampler.distance(below), sampler.distance(above), 1.0e-3f);
    EXPECT_NEAR(glm::length(sampler.gradient(below) - sampler.gradient(above)), 0.0f, 1.0e-3f);
  }
}

TEST(TerrainSamplingTest, UniformRegionHasZeroGradientAndAFiniteMetricDistance)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ 0, 0, 0 }, terrain::TerrainChunk::uniform(-terrain::airDistance, 1));
  volume.setChunk({ 1, 0, 0 }, terrain::TerrainChunk::uniform(-terrain::airDistance, 1));
  terrain::Sampler sampler(volume);

  const glm::vec3 inside(70.0f, 10.0f, 10.0f);
  const glm::vec3 gradient = sampler.gradient(inside);
  const float metric = sampler.metricDistance(inside);

  EXPECT_EQ(gradient, glm::vec3(0.0f));
  EXPECT_FLOAT_EQ(sampler.distance(inside), -terrain::bandVoxels);
  EXPECT_TRUE(std::isfinite(metric));
  EXPECT_NEAR(metric, -terrain::bandVoxels * 2.5f, 1.0e-3f);

  // Positive control: a field with a real gradient is not floored.
  terrain::TerrainVolume sloped;
  fillField(sloped, plane(0.0f));
  terrain::Sampler slopedSampler(sloped);
  EXPECT_GT(glm::length(slopedSampler.gradient({ 1.0f, 1.0f, 1.0f })), 0.3f);
  EXPECT_NEAR(slopedSampler.metricDistance({ 1.0f, 1.0f, 1.0f }), 1.0f, 1.0e-2f);
  EXPECT_NEAR(slopedSampler.metricDistance({ 1.0f, -1.5f, 1.0f }), -1.5f, 1.0e-2f);
}

TEST(TerrainSamplingTest, AbsentChunksReadAsAir)
{
  const terrain::TerrainVolume volume;
  terrain::Sampler sampler(volume);

  EXPECT_FLOAT_EQ(sampler.distance({ 3.0f, 4.0f, 5.0f }), terrain::bandVoxels);
  EXPECT_EQ(sampler.gradient({ 3.0f, 4.0f, 5.0f }), glm::vec3(0.0f));
  EXPECT_TRUE(std::isfinite(sampler.metricDistance({ 3.0f, 4.0f, 5.0f })));
  EXPECT_FLOAT_EQ(sampler.distance({ 3.0f, 4.0f, 5.0f }), terrain::bandVoxels);
}

TEST(TerrainSamplingTest, NonFinitePositionsAreAnswered)
{
  terrain::TerrainVolume volume;
  fillField(volume, plane(0.0f));
  terrain::Sampler sampler(volume);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();

  const float airMetric = terrain::bandVoxels * 2.5f;

  for (const glm::vec3 position :
       { glm::vec3(nan, 0.0f, 0.0f), glm::vec3(0.0f, inf, 0.0f), glm::vec3(0.0f, 0.0f, -inf) })
  {
    EXPECT_EQ(sampler.distance(position), terrain::bandVoxels);
    EXPECT_EQ(sampler.gradient(position), glm::vec3(0.0f));
    EXPECT_EQ(sampler.metricDistance(position), airMetric);
  }

  EXPECT_TRUE(std::isfinite(sampler.distance({ 1.0e30f, 0.0f, 0.0f })));
  EXPECT_NE(sampler.distance({ 0.0f, 1.0f, 0.0f }), terrain::bandVoxels);
}

TEST(TerrainSamplingTest, AReusedSamplerMatchesAFreshOnePerQuery)
{
  const glm::vec3 normal = glm::normalize(glm::vec3(0.4f, 1.0f, 0.1f));
  terrain::TerrainVolume volume;
  fillField(volume, tiltedPlane(normal, 2.0f));
  // Leave a hole so absent chunks are cached too.
  volume.setChunk({ 1, 1, 1 }, nullptr);
  volume.setChunk({ -1, -1, -1 }, nullptr);

  terrain::Sampler reused(volume);
  std::mt19937 rng(99);

  for (int i = 0; i < 2000; ++i)
  {
    const glm::vec3 point = randomPointNear(rng, 190.0f);
    terrain::Sampler fresh(volume);

    EXPECT_EQ(reused.distance(point), fresh.distance(point));
    EXPECT_EQ(reused.gradient(point), fresh.gradient(point));
    EXPECT_EQ(reused.metricDistance(point), fresh.metricDistance(point));
  }
}

TEST(TerrainSamplingTest, AHeldChunkStaysValidAfterTheVolumeSwapsIt)
{
  terrain::TerrainVolume volume;
  volume.setChunk({ 0, 0, 0 }, terrain::TerrainChunk::uniform(-1000, 1));
  terrain::Sampler sampler(volume);
  const glm::vec3 point(10.0f, 10.0f, 10.0f);
  const float before = sampler.distance(point);

  volume.setChunk({ 0, 0, 0 }, terrain::TerrainChunk::uniform(500, 1));

  // The live Sampler still holds the chunk it fetched, so it keeps answering from it.
  EXPECT_FLOAT_EQ(sampler.distance(point), before);
  EXPECT_NEAR(before, -1000.0f / 8191.0f, 1.0e-5f);
  EXPECT_NEAR(terrain::Sampler(volume).distance(point), 500.0f / 8191.0f, 1.0e-5f);
}

TEST(TerrainSamplingTest, MetricDistanceIsClampedToTheBandWhereCornersAreClamped)
{
  // The plane sits half a voxel above the sample row at y = 0, so the corner at y = 5 is clamped at 4 voxels.
  terrain::TerrainVolume volume;
  fillField(volume, plane(0.5f * 2.5f));
  terrain::Sampler sampler(volume);
  const float reach = terrain::bandVoxels * 2.5f;

  const glm::vec3 above(7.0f, 4.9f * 2.5f, 7.0f);
  const glm::vec3 below(7.0f, -4.9f * 2.5f + 2.5f, 7.0f);

  // d / |g| would be 7.9 voxels here without the clamp.
  EXPECT_NEAR(sampler.distance(above), 3.95f, 1.0e-3f);
  EXPECT_NEAR(glm::length(sampler.gradient(above)), 0.5f / 2.5f, 1.0e-3f);
  EXPECT_FLOAT_EQ(sampler.metricDistance(above), reach);

  terrain::TerrainVolume inverted;
  fillField(inverted, [](const glm::vec3 p) { return -(p.y - 0.5f * 2.5f); });
  terrain::Sampler invertedSampler(inverted);
  EXPECT_FLOAT_EQ(invertedSampler.metricDistance(above), -reach);
  EXPECT_LE(std::abs(sampler.metricDistance(below)), reach);

  // Positive control: near the surface the metric distance is the exact distance and not clamped.
  const glm::vec3 nearSurface(7.0f, 1.5f * 2.5f, 7.0f);
  EXPECT_NEAR(sampler.metricDistance(nearSurface), 1.0f * 2.5f, 1.0e-2f);
}

TEST(TerrainSamplingTest, GradientMatchesACentralDifferenceOfDistanceOnRandomData)
{
  std::mt19937 rng(2024);
  std::uniform_int_distribution<int> distance(-terrain::airDistance, terrain::airDistance);
  std::uniform_int_distribution<int> material(0, 10);
  terrain::TerrainVolume volume;

  // Chunks -1 and 0 on every axis, so cells cross the chunk borders at sample 0 and the volume has a negative side.
  for (const terrain::ChunkCoord coord : terrain::chunksTouched({ { -32, -32, -32 }, { 32, 32, 32 } }))
  {
    std::vector<int16_t> distances(terrain::samplesPerChunk);
    std::vector<uint8_t> materials(terrain::samplesPerChunk);

    for (size_t i = 0; i < distances.size(); ++i)
    {
      distances[i] = static_cast<int16_t>(distance(rng));
      materials[i] = static_cast<uint8_t>(material(rng));
    }

    volume.setChunk(coord, terrain::TerrainChunk::dense(std::move(distances), std::move(materials)));
  }

  const float voxelSize = volume.voxelSize();
  terrain::Sampler sampler(volume);
  std::uniform_int_distribution<int> corner(-32, 30);
  std::uniform_real_distribution<float> inside(0.25f, 0.75f);
  // A tenth of a voxel stays inside the cell for fractions in [0.25, 0.75].
  const float step = 0.1f * voxelSize;

  int nearBorder = 0;

  for (int i = 0; i < 300; ++i)
  {
    glm::ivec3 base(corner(rng), corner(rng), corner(rng));

    if (i % 3 == 0)
    {
      // Force a cell straddling the chunk border at sample 0 on a random axis.
      const int axis = i % 9 / 3;
      base[axis] = -1;
    }

    const glm::vec3 voxelPoint = glm::vec3(base) + glm::vec3(inside(rng), inside(rng), inside(rng));
    const glm::vec3 point = voxelPoint * voxelSize;
    const glm::vec3 analytic = sampler.gradient(point);

    for (int axis = 0; axis < 3; ++axis)
    {
      glm::vec3 forward = point;
      glm::vec3 backward = point;
      forward[axis] += step;
      backward[axis] -= step;
      const float numeric = (sampler.distance(forward) - sampler.distance(backward)) / (2.0f * step);

      EXPECT_NEAR(analytic[axis], numeric, 1.0e-3f) << i << " axis " << axis;
    }

    if (base.x == -1 || base.y == -1 || base.z == -1)
    {
      ++nearBorder;
    }
  }

  EXPECT_GT(nearBorder, 50);
}

namespace {
  // Random dense chunks over samples -32..32 on every axis, with a uniform and an absent chunk among them, so cells
  // straddle each kind of chunk border.
  void fillMixedChunks(terrain::TerrainVolume& volume, std::mt19937& rng)
  {
    std::uniform_int_distribution<int> distance(-terrain::airDistance, terrain::airDistance);
    for (const terrain::ChunkCoord coord : terrain::chunksTouched({ { -32, -32, -32 }, { 32, 32, 32 } }))
    {
      std::vector<int16_t> distances(terrain::samplesPerChunk);
      for (auto& value : distances)
      {
        value = static_cast<int16_t>(distance(rng));
      }

      volume.setChunk(coord, terrain::TerrainChunk::dense(std::move(distances),
                                                          std::vector<uint8_t>(terrain::samplesPerChunk, 1)));
    }

    volume.setChunk({ 0, 0, 0 }, terrain::TerrainChunk::uniform(-1234, 1));
    volume.setChunk({ -1, 0, -1 }, nullptr);
  }

  // The trilinear distance at point from the volume's own samples, read one corner at a time.
  float cornerByCornerDistance(const terrain::TerrainVolume& volume, const glm::vec3& point)
  {
    const glm::vec3 position = point / volume.voxelSize();
    const glm::vec3 base = glm::floor(position);
    const glm::vec3 f = position - base;
    const glm::ivec3 origin(base);

    std::array<float, 8> c{};
    for (int corner = 0; corner < 8; ++corner)
    {
      const glm::ivec3 offset(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
      c[static_cast<size_t>(corner)] = terrain::decodeDistance(volume.distanceAtSample(origin + offset));
    }

    const float x00 = c[0] + (c[1] - c[0]) * f.x;
    const float x10 = c[2] + (c[3] - c[2]) * f.x;
    const float x01 = c[4] + (c[5] - c[4]) * f.x;
    const float x11 = c[6] + (c[7] - c[6]) * f.x;
    const float y0 = x00 + (x10 - x00) * f.y;
    const float y1 = x01 + (x11 - x01) * f.y;

    return y0 + (y1 - y0) * f.z;
  }

  bool onTheLastCellOfAChunk(const glm::vec3& samplePoint)
  {
    const glm::ivec3 inChunk = (glm::ivec3(glm::floor(samplePoint)) % terrain::chunkSize + terrain::chunkSize) %
      terrain::chunkSize;

    return inChunk.x == terrain::chunkSize - 1 || inChunk.y == terrain::chunkSize - 1 ||
      inChunk.z == terrain::chunkSize - 1;
  }
}

TEST(TerrainSamplingTest, EveryCellReadsTheCornerSamplesTheVolumeHolds)
{
  std::mt19937 rng(77);
  terrain::TerrainVolume volume;
  fillMixedChunks(volume, rng);

  terrain::Sampler sampler(volume);
  std::uniform_real_distribution<float> sampleAxis(-33.0f, 33.0f);

  int acrossBorders = 0;
  for (int i = 0; i < 4000; ++i)
  {
    glm::vec3 samplePoint(sampleAxis(rng), sampleAxis(rng), sampleAxis(rng));
    if (i % 4 == 0)
    {
      // On the last cell of a chunk along a random axis, whose far corners lie in the next chunk.
      samplePoint[i % 12 / 4] = (i % 8 == 0 ? 31.0f : -1.0f) + 0.5f;
    }

    const glm::vec3 point = samplePoint * volume.voxelSize();
    EXPECT_NEAR(sampler.distance(point), cornerByCornerDistance(volume, point), 1.0e-6f) << i;
    acrossBorders += onTheLastCellOfAChunk(samplePoint) ? 1 : 0;
  }

  EXPECT_GT(acrossBorders, 500);
}

TEST(TerrainSamplingTest, RepeatedAndInterleavedQueriesAnswerAsAFreshSamplerDoes)
{
  const glm::vec3 normal = glm::normalize(glm::vec3(0.4f, 1.0f, 0.1f));
  terrain::TerrainVolume volume;
  fillField(volume, tiltedPlane(normal, 2.0f));

  const glm::vec3 first(3.1f, 1.7f, -4.2f);
  const glm::vec3 second(-20.0f, 9.3f, 60.5f);
  const glm::vec3 third(79.9f, -3.0f, 0.4f);

  terrain::Sampler reused(volume);
  for (const glm::vec3& point : { first, first, second, first, third, second, second, third, first })
  {
    terrain::Sampler fresh(volume);

    EXPECT_EQ(reused.gradient(point), fresh.gradient(point));
    EXPECT_EQ(reused.metricDistance(point), fresh.metricDistance(point));
    EXPECT_EQ(reused.distance(point), fresh.distance(point));
  }

  // Positive control: the three positions read different distances, so a stale cell would show.
  terrain::Sampler control(volume);
  EXPECT_NE(control.distance(first), control.distance(second));
  EXPECT_NE(control.distance(second), control.distance(third));
}

TEST(TerrainSamplingTest, VoxelSizeChangesUnitsButNotVoxelTerms)
{
  const glm::vec3 normal = glm::normalize(glm::vec3(0.2f, 1.0f, 0.3f));
  terrain::TerrainVolume fine(1.0f);
  terrain::TerrainVolume coarse(2.5f);
  fillField(fine, tiltedPlane(normal, 1.0f));
  fillField(coarse, tiltedPlane(normal, 2.5f));
  terrain::Sampler fineSampler(fine);
  terrain::Sampler coarseSampler(coarse);
  std::mt19937 rng(5);

  int checked = 0;

  for (int i = 0; i < 300; ++i)
  {
    const glm::vec3 voxelPoint = randomPointNear(rng, 60.0f);
    const float exactVoxels = glm::dot(voxelPoint, normal) - 1.0f;

    if (std::abs(exactVoxels) > 2.0f)
    {
      continue;
    }

    ++checked;
    EXPECT_NEAR(fineSampler.distance(voxelPoint), exactVoxels, quantizationTolerance);
    EXPECT_NEAR(coarseSampler.distance(voxelPoint * 2.5f), exactVoxels, quantizationTolerance);
    EXPECT_NEAR(glm::length(fineSampler.gradient(voxelPoint)), 1.0f, 1.0e-3f);
    EXPECT_NEAR(glm::length(coarseSampler.gradient(voxelPoint * 2.5f)), 1.0f / 2.5f, 1.0e-3f);
    EXPECT_NEAR(coarseSampler.metricDistance(voxelPoint * 2.5f), exactVoxels * 2.5f, 1.0e-2f);
  }

  EXPECT_GT(checked, 10);
}
