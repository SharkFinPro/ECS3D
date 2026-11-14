#include <gtest/gtest.h>

#include "terrain/TerrainChunk.h"
#include "terrain/TerrainSampling.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <random>
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
  };

  // Chunk borders sit at sample 32 (80 units) and sample -32 (-80 units).
  for (const Case c : { Case{ { 80.0f, 0.0f, 0.0f }, { 80.0f, 30.0f, 0.0f } },
                        Case{ { -80.0f, 0.0f, 0.0f }, { -80.0f, 0.0f, 30.0f } },
                        Case{ { 0.0f, 80.0f, 0.0f }, { 0.0f, 80.0f, 30.0f } } })
  {
    terrain::TerrainVolume volume;
    fillField(volume, sphere(c.center, 30.0f));
    terrain::Sampler sampler(volume);

    const float axisStep = 1.0e-3f;
    glm::vec3 below = c.probe;
    glm::vec3 above = c.probe;
    const int axis = (c.center.x != 0.0f) ? 0 : 1;
    below[axis] -= axisStep;
    above[axis] += axisStep;

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
  EXPECT_NEAR(metric, -terrain::bandVoxels * 2.5f / terrain::minGradientLength, 1.0e-2f);

  // Positive control: a field with a real gradient is not floored.
  terrain::TerrainVolume sloped;
  fillField(sloped, plane(0.0f));
  terrain::Sampler slopedSampler(sloped);
  EXPECT_GT(glm::length(slopedSampler.gradient({ 1.0f, 1.0f, 1.0f })), 0.3f);
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

  EXPECT_TRUE(std::isfinite(sampler.distance({ nan, 0.0f, 0.0f })));
  EXPECT_TRUE(std::isfinite(sampler.distance({ 0.0f, inf, 0.0f })));
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

  EXPECT_NEAR(before, -1000.0f / 8191.0f, 1.0e-5f);
  EXPECT_NEAR(terrain::Sampler(volume).distance(point), 500.0f / 8191.0f, 1.0e-5f);
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
