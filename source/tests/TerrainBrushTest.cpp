#include <gtest/gtest.h>

#include "terrain/TerrainBrush.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainGenerators.h"
#include "terrain/TerrainOps.h"
#include "terrain/TerrainSampling.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <glm/geometric.hpp>
#include <glm/vector_relational.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace {
  constexpr float voxel = 2.5f;
  constexpr float groundHeight = 10.0f;
  constexpr glm::vec3 craterCenter{ 80.0f, 10.0f, 80.0f };

  using Kind = terrain::BrushOp::Kind;

  terrain::TerrainVolume makeGround()
  {
    terrain::TerrainVolume volume(voxel);
    terrain::commit(volume, terrain::fillFlat(volume, { 64, 64 }, groundHeight, 20.0f, 1));
    return volume;
  }

  terrain::BrushOp brush(const Kind kind, const glm::vec3 center, const float radius)
  {
    terrain::BrushOp op;
    op.kind = kind;
    op.center = center;
    op.radius = radius;
    op.material = 7;
    return op;
  }

  void commitBrush(terrain::TerrainVolume& volume, const terrain::BrushOp& op)
  {
    terrain::commit(volume, terrain::applyBrush(volume, op));
  }

  float distanceAt(const terrain::TerrainVolume& volume, const int x, const int y, const int z)
  {
    return terrain::decodeDistance(volume.distanceAtSample({ x, y, z }));
  }

  float maxGradient(const terrain::TerrainVolume& volume, const glm::vec3 low, const glm::vec3 high)
  {
    terrain::Sampler sampler(volume);
    float largest = 0.0f;

    for (float z = low.z; z <= high.z; z += 1.25f)
    {
      for (float y = low.y; y <= high.y; y += 1.25f)
      {
        for (float x = low.x; x <= high.x; x += 1.25f)
        {
          largest = std::max(largest, glm::length(sampler.gradient({ x, y, z })));
        }
      }
    }

    return largest;
  }

  bool changedAt(const terrain::TerrainVolume& before, const terrain::TerrainVolume& after, const glm::ivec3 sample)
  {
    return before.distanceAtSample(sample) != after.distanceAtSample(sample) ||
           before.materialAtSample(sample) != after.materialAtSample(sample);
  }

  bool contains(const terrain::SampleBox& box, const glm::ivec3 sample)
  {
    return glm::all(glm::greaterThanEqual(sample, box.min)) && glm::all(glm::lessThan(sample, box.max));
  }

  // Counts samples inside the influence sphere that did not change or fell outside the dirty box.
  int coverageFailures(const terrain::TerrainVolume& volume, const terrain::BrushOp& op, int& covered)
  {
    const terrain::SampleBox box = terrain::dirtyBox(volume, op);
    terrain::TerrainVolume after = volume;
    terrain::commit(after, terrain::applyBrush(volume, op));

    const float influence = op.radius + terrain::brushBorderVoxels * voxel;
    int failures = 0;
    covered = 0;

    for (int z = box.min.z - 3; z < box.max.z + 3; ++z)
    {
      for (int y = box.min.y - 3; y < box.max.y + 3; ++y)
      {
        for (int x = box.min.x - 3; x < box.max.x + 3; ++x)
        {
          const glm::ivec3 sample(x, y, z);

          if (glm::length(glm::vec3(sample) * voxel - op.center) > influence - 1e-3f)
          {
            continue;
          }

          ++covered;
          failures += contains(box, sample) && changedAt(volume, after, sample) ? 0 : 1;
        }
      }
    }

    return failures;
  }

  terrain::BrushOp sampleOp(const Kind kind)
  {
    terrain::BrushOp op = brush(kind, { 80.0f, 12.0f, 80.0f }, 20.0f);
    op.strength = 0.8f;
    op.falloff = 0.5f;
    op.planePoint = { 80.0f, 14.0f, 80.0f };
    op.planeNormal = { 0.1f, 1.0f, 0.0f };
    return op;
  }

}

TEST(TerrainBrushTest, AddBuildsASphereIntoEmptySpace)
{
  terrain::TerrainVolume volume(voxel);
  const terrain::BrushOp op = brush(Kind::add, { 50.0f, 50.0f, 50.0f }, 25.0f);

  EXPECT_EQ(volume.chunks().size(), 0u);
  commitBrush(volume, op);
  ASSERT_FALSE(volume.chunks().empty());

  terrain::Sampler sampler(volume);

  for (const glm::vec3 direction : { glm::vec3(1, 0, 0), glm::vec3(-1, 0, 0), glm::vec3(0, 1, 0),
                                     glm::vec3(0, -1, 0), glm::vec3(0, 0, 1), glm::vec3(0, 0, -1),
                                     glm::normalize(glm::vec3(1, 1, 1)), glm::normalize(glm::vec3(-1, 1, 1)),
                                     glm::normalize(glm::vec3(1, -1, 1)), glm::normalize(glm::vec3(1, 1, -1)),
                                     glm::normalize(glm::vec3(-1, -1, 1)), glm::normalize(glm::vec3(-1, 1, -1)),
                                     glm::normalize(glm::vec3(1, -1, -1)), glm::normalize(glm::vec3(-1, -1, -1)) })
  {
    EXPECT_NEAR(sampler.distance(op.center + direction * op.radius), 0.0f, 0.05f);
    EXPECT_LT(sampler.distance(op.center + direction * op.radius * 0.5f), -1.0f);
    EXPECT_GT(sampler.distance(op.center + direction * (op.radius + 3.0f * voxel)), 1.0f);
  }

  EXPECT_NEAR(sampler.distance(op.center), -terrain::bandVoxels, 1e-3f);
  EXPECT_EQ(volume.materialAtSample({ 20, 20, 20 }), 7);
}

TEST(TerrainBrushTest, AddRewritesEveryCornerOfEverySurfaceCell)
{
  terrain::TerrainVolume volume(voxel);
  const terrain::BrushOp op = brush(Kind::add, { 50.0f, 50.0f, 50.0f }, 25.0f);
  commitBrush(volume, op);

  const float reachVoxels = std::sqrt(3.0f);
  int checked = 0;
  int beyondOneVoxel = 0;

  for (int z = 0; z <= 40; ++z)
  {
    for (int y = 0; y <= 40; ++y)
    {
      for (int x = 0; x <= 40; ++x)
      {
        const float sphere = (glm::length(glm::vec3(x, y, z) * voxel - op.center) - op.radius) / voxel;

        if (sphere > reachVoxels)
        {
          continue;
        }

        ++checked;
        beyondOneVoxel += sphere > 1.0f ? 1 : 0;
        ASSERT_NEAR(distanceAt(volume, x, y, z), std::clamp(sphere, -terrain::bandVoxels, terrain::bandVoxels), 2e-3f);
      }
    }
  }

  EXPECT_GT(checked, 0);
  EXPECT_GT(beyondOneVoxel, 0);
}

TEST(TerrainBrushTest, SubtractCarvesACraterOutOfGround)
{
  terrain::TerrainVolume volume = makeGround();
  const glm::vec3 center(80.0f, 5.0f, 80.0f);

  {
    terrain::Sampler before(volume);
    EXPECT_LT(before.distance(center), -1.0f);
  }

  commitBrush(volume, brush(Kind::subtract, center, 12.5f));

  terrain::Sampler sampler(volume);
  EXPECT_GT(sampler.distance(center), 1.0f);
  EXPECT_NEAR(sampler.distance(center - glm::vec3(0.0f, 12.5f, 0.0f)), 0.0f, 0.05f);
}

TEST(TerrainBrushTest, SmoothReducesAStep)
{
  terrain::TerrainVolume volume(voxel);
  const terrain::SampleBox box{ { 0, -8, 0 }, { 64, 16, 64 } };

  terrain::commit(volume, terrain::editSamples(volume, box, [](const glm::ivec3 sample, int16_t& distance, uint8_t& material)
  {
    const float height = sample.x < 32 ? 10.0f : 20.0f;
    distance = terrain::encodeDistance((static_cast<float>(sample.y) * voxel - height) / voxel);
    material = 1;
  }));

  const glm::vec3 low(70.0f, 5.0f, 70.0f);
  const glm::vec3 high(90.0f, 25.0f, 90.0f);
  const float before = maxGradient(volume, low, high);
  EXPECT_GT(before, 1.0f);

  terrain::BrushOp op = brush(Kind::smooth, { 80.0f, 15.0f, 80.0f }, 25.0f);
  commitBrush(volume, op);

  EXPECT_LT(maxGradient(volume, low, high), before * 0.6f);
}

TEST(TerrainBrushTest, FlattenMovesTheSurfaceToThePlane)
{
  terrain::TerrainVolume volume = makeGround();
  terrain::BrushOp op = brush(Kind::flatten, craterCenter, 25.0f);
  op.material = 9;
  op.planePoint = { 80.0f, 15.0f, 80.0f };

  {
    terrain::Sampler before(volume);
    EXPECT_NEAR(before.distance({ 80.0f, 15.0f, 80.0f }), 2.0f, 1e-3f);
  }

  EXPECT_EQ(volume.materialAtSample({ 32, 5, 32 }), 1);
  commitBrush(volume, op);

  terrain::Sampler sampler(volume);

  for (const glm::vec3 point : { glm::vec3(80.0f, 15.0f, 80.0f), glm::vec3(70.0f, 15.0f, 70.0f),
                                 glm::vec3(85.0f, 15.0f, 75.0f) })
  {
    EXPECT_NEAR(sampler.distance(point), 0.0f, 0.05f);
  }

  EXPECT_EQ(volume.materialAtSample({ 32, 5, 32 }), 9);
}

TEST(TerrainBrushTest, PaintChangesOnlyMaterialNearTheSurface)
{
  terrain::TerrainVolume volume = makeGround();
  const terrain::TerrainVolume original = volume;
  terrain::BrushOp op = brush(Kind::paint, craterCenter, 15.0f);
  op.material = 5;

  const terrain::ChunkEdits edits = terrain::applyBrush(volume, op);
  EXPECT_FALSE(edits.empty());
  terrain::commit(volume, edits);

  const terrain::SampleBox box = terrain::dirtyBox(original, op);
  ASSERT_FALSE(box.empty());

  for (int z = box.min.z; z < box.max.z; ++z)
  {
    for (int y = box.min.y; y < box.max.y; ++y)
    {
      for (int x = box.min.x; x < box.max.x; ++x)
      {
        ASSERT_EQ(original.distanceAtSample({ x, y, z }), volume.distanceAtSample({ x, y, z }));
      }
    }
  }

  EXPECT_EQ(original.materialAtSample({ 32, 4, 32 }), 1);
  EXPECT_EQ(volume.materialAtSample({ 32, 4, 32 }), 5);
  EXPECT_EQ(volume.materialAtSample({ 32, 3, 32 }), 5);
  EXPECT_EQ(volume.materialAtSample({ 32, 0, 32 }), 1);
  EXPECT_EQ(volume.materialAtSample({ 32, 8, 32 }), 1);
  EXPECT_EQ(volume.materialAtSample({ 32, 5, 32 }), 5);
  EXPECT_EQ(volume.materialAtSample({ 32, 6, 32 }), 1);
  EXPECT_EQ(volume.materialAtSample({ 32, 2, 32 }), 1);
  EXPECT_EQ(volume.materialAtSample({ 39, 4, 32 }), 1);
}

TEST(TerrainBrushTest, DirtyBoxMatchesTheInfluenceBounds)
{
  const terrain::TerrainVolume volume(voxel);

  const terrain::SampleBox aligned = terrain::dirtyBox(volume, brush(Kind::add, { 50.0f, 50.0f, 50.0f }, 25.0f));
  EXPECT_EQ(aligned.min, glm::ivec3(8));
  EXPECT_EQ(aligned.max, glm::ivec3(33));

  const terrain::SampleBox shifted = terrain::dirtyBox(volume, brush(Kind::add, { 51.0f, -51.0f, 50.0f }, 25.0f));
  EXPECT_EQ(shifted.min, glm::ivec3(8, -33, 8));
  EXPECT_EQ(shifted.max, glm::ivec3(34, -7, 33));
}

TEST(TerrainBrushTest, EveryKernelChangesEverySampleWithinTheInfluence)
{
  terrain::TerrainVolume solid(voxel);
  terrain::commit(solid, terrain::fillFlat(solid, { 64, 64 }, groundHeight, 100.0f, 1));

  terrain::BrushOp flatten = brush(Kind::flatten, { 80.0f, -30.0f, 80.0f }, 25.0f);
  flatten.planePoint = { 80.0f, -1000.0f, 80.0f };

  const terrain::TerrainVolume empty(voxel);
  const std::vector<std::pair<const terrain::TerrainVolume*, terrain::BrushOp>> cases = {
    { &empty, brush(Kind::add, { 51.0f, 50.0f, 49.0f }, 25.0f) },
    { &solid, brush(Kind::subtract, { 80.0f, -30.0f, 80.0f }, 25.0f) },
    { &solid, flatten },
  };

  for (const auto& [volume, op] : cases)
  {
    int covered = 0;
    EXPECT_EQ(coverageFailures(*volume, op, covered), 0) << "kind " << static_cast<int>(op.kind);
    EXPECT_GT(covered, 1000);
  }
}

TEST(TerrainBrushTest, WeightFollowsStrengthAndFalloff)
{
  struct Row
  {
    float falloff;
    float strength;
    int step;
    float expected;
  };

  // An add into air at sample (20 + step, 20, 20), radius 25 around sample (20, 20, 20): the target is the sphere
  // distance (r - 25) / 2.5 and the weight ramps from the inner radius to the influence radius at 30.
  const Row rows[] = {
    { 0.5f, 0.5f, 4, -1.0f },       // inside the inner radius: full strength
    { 0.5f, 0.5f, 9, 2.75f },       // ramp midpoint: smoothstep 0.5
    { 0.5f, 0.5f, 11, 3.888889f },  // just inside the influence radius
    { 0.5f, 0.5f, 13, 4.0f },       // beyond it
    { 1.0f, 1.0f, 0, -4.0f },
    { 1.0f, 1.0f, 6, 0.0f },
    { 1.0f, 1.0f, 11, 3.941f },
  };

  for (const Row& row : rows)
  {
    terrain::TerrainVolume volume(voxel);
    terrain::BrushOp op = brush(Kind::add, { 50.0f, 50.0f, 50.0f }, 25.0f);
    op.falloff = row.falloff;
    op.strength = row.strength;
    commitBrush(volume, op);

    EXPECT_NEAR(distanceAt(volume, 20 + row.step, 20, 20), row.expected, 1e-3f)
      << "falloff " << row.falloff << " step " << row.step;
  }
}

TEST(TerrainBrushTest, SmoothMatchesABruteForceMeanOnANonSeparableField)
{
  terrain::TerrainVolume volume(voxel);
  const terrain::SampleBox fieldBox{ { 0, 0, 0 }, { 41, 41, 41 } };

  terrain::commit(volume, terrain::editSamples(volume, fieldBox, [](const glm::ivec3 sample, int16_t& distance, uint8_t&)
  {
    const glm::vec3 offset = glm::vec3(sample) * voxel - glm::vec3(55.0f, 48.0f, 52.0f);
    distance = terrain::encodeDistance((glm::length(offset) - 30.0f) / voxel);
  }));

  const terrain::TerrainVolume original = volume;
  const terrain::BrushOp op = brush(Kind::smooth, { 50.0f, 50.0f, 50.0f }, 20.0f);
  const terrain::SampleBox box = terrain::dirtyBox(original, op);
  commitBrush(volume, op);

  int smoothed = 0;

  for (int z = box.min.z; z < box.max.z; ++z)
  {
    for (int y = box.min.y; y < box.max.y; ++y)
    {
      for (int x = box.min.x; x < box.max.x; ++x)
      {
        float expected = distanceAt(original, x, y, z);

        if (glm::length(glm::vec3(x, y, z) * voxel - op.center) <= 25.0f)
        {
          float sum = 0.0f;

          for (int dz = -1; dz <= 1; ++dz)
          {
            for (int dy = -1; dy <= 1; ++dy)
            {
              for (int dx = -1; dx <= 1; ++dx)
              {
                sum += distanceAt(original, x + dx, y + dy, z + dz);
              }
            }
          }

          expected = sum / 27.0f;
          ++smoothed;
        }

        ASSERT_NEAR(distanceAt(volume, x, y, z), expected, 1e-3f) << x << " " << y << " " << z;
      }
    }
  }

  EXPECT_GT(smoothed, 1000);
}

TEST(TerrainBrushTest, FlattenUsesTheNormalizedTiltedPlane)
{
  const glm::vec3 point(55.0f, 50.0f, 50.0f);

  for (const glm::vec3 normal : { glm::vec3(0.1f, 2.0f, 0.0f), glm::vec3(1.0e30f, 1.0e30f, 0.0f) })
  {
    terrain::TerrainVolume volume(voxel);
    terrain::BrushOp op = brush(Kind::flatten, { 50.0f, 50.0f, 50.0f }, 25.0f);
    op.planePoint = point;
    op.planeNormal = normal;
    commitBrush(volume, op);

    const glm::vec3 unit = glm::vec3(glm::normalize(glm::dvec3(normal)));

    for (const glm::ivec3 sample : { glm::ivec3(20, 20, 20), glm::ivec3(20, 21, 20), glm::ivec3(22, 19, 21) })
    {
      const float expected = glm::dot(glm::vec3(sample) * voxel - point, unit) / voxel;
      EXPECT_NEAR(distanceAt(volume, sample.x, sample.y, sample.z), expected, 1e-3f);
    }
  }
}

TEST(TerrainBrushTest, ZeroStrengthFlattenNeverTurnsGroundIntoAir)
{
  const terrain::TerrainVolume volume = makeGround();
  terrain::BrushOp op = brush(Kind::flatten, craterCenter, 15.0f);
  op.planePoint = { 80.0f, 15.0f, 80.0f };
  EXPECT_FALSE(terrain::applyBrush(volume, op).empty());

  op.strength = 0.0f;
  EXPECT_TRUE(terrain::applyBrush(volume, op).empty());

  const terrain::TerrainVolume fine(0.5f);
  op.planePoint = { 3.0e38f, 0.0f, 0.0f };
  EXPECT_TRUE(terrain::dirtyBox(fine, op).empty());
  EXPECT_TRUE(terrain::applyBrush(fine, op).empty());

  op.planePoint = { 80.0f, 15.0f, 80.0f };
  op.planeNormal = { 1.0e-30f, 0.0f, 0.0f };
  EXPECT_TRUE(terrain::applyBrush(volume, op).empty());
}

TEST(TerrainBrushTest, RadiusAndCenterBoundariesAreExact)
{
  const terrain::TerrainVolume volume = makeGround();
  terrain::BrushOp op = brush(Kind::add, craterCenter, terrain::maxBrushRadiusVoxels * voxel);

  EXPECT_FALSE(terrain::dirtyBox(volume, op).empty());
  EXPECT_FALSE(terrain::applyBrush(volume, op).empty());

  op.radius = std::nextafter(op.radius, 1.0e9f);
  EXPECT_TRUE(terrain::dirtyBox(volume, op).empty());
  EXPECT_TRUE(terrain::applyBrush(volume, op).empty());

  const terrain::TerrainVolume unit(1.0f);
  const double bound = static_cast<double>(terrain::maxChunkCoord - 1) * terrain::chunkSize -
                       (terrain::maxBrushRadiusVoxels + terrain::brushBorderVoxels + 2.0);

  for (const float sign : { 1.0f, -1.0f })
  {
    const float inside = std::nextafter(static_cast<float>(bound), 0.0f);
    const float outside = static_cast<float>(1 << 29);

    EXPECT_FALSE(terrain::dirtyBox(unit, brush(Kind::add, { sign * inside, 0.0f, 0.0f }, 1.0f)).empty());
    EXPECT_FALSE(terrain::applyBrush(unit, brush(Kind::add, { sign * inside, 0.0f, 0.0f }, 1.0f)).empty());
    EXPECT_TRUE(terrain::dirtyBox(unit, brush(Kind::add, { sign * outside, 0.0f, 0.0f }, 1.0f)).empty());
    EXPECT_TRUE(terrain::applyBrush(unit, brush(Kind::add, { sign * outside, 0.0f, 0.0f }, 1.0f)).empty());
  }
}

TEST(TerrainBrushTest, ApplyBrushDoesNotMutateTheVolume)
{
  terrain::TerrainVolume volume = makeGround();
  const auto before = volume.snapshotChunks();

  const terrain::ChunkEdits edits = terrain::applyBrush(volume, sampleOp(Kind::add));
  EXPECT_FALSE(edits.empty());

  const auto after = volume.snapshotChunks();
  ASSERT_EQ(before.size(), after.size());

  for (const auto& [coord, chunk] : before)
  {
    ASSERT_TRUE(after.contains(coord));
    EXPECT_EQ(after.at(coord), chunk);
    EXPECT_EQ(after.at(coord)->hash(), chunk->hash());
  }
}

TEST(TerrainBrushTest, ABrushAtAChunkCornerTouchesEightChunks)
{
  for (const glm::vec3 center : { glm::vec3(0.0f), glm::vec3(-80.0f), glm::vec3(80.0f, -80.0f, 80.0f) })
  {
    terrain::TerrainVolume volume(voxel);
    const terrain::BrushOp op = brush(Kind::add, center, 10.0f);
    const terrain::ChunkEdits edits = terrain::applyBrush(volume, op);

    EXPECT_EQ(edits.size(), 8u);
    terrain::commit(volume, edits);

    terrain::Sampler sampler(volume);
    EXPECT_NEAR(sampler.distance(center + glm::vec3(10.0f, 0.0f, 0.0f)), 0.0f, 0.05f);
    EXPECT_NEAR(sampler.distance(center - glm::vec3(0.0f, 10.0f, 0.0f)), 0.0f, 0.05f);
    EXPECT_NEAR(sampler.distance(center), -terrain::bandVoxels, 1e-3f);
  }
}

TEST(TerrainBrushTest, ZeroStrengthChangesNothing)
{
  const terrain::TerrainVolume volume = makeGround();
  terrain::BrushOp op = brush(Kind::add, craterCenter, 15.0f);

  EXPECT_FALSE(terrain::applyBrush(volume, op).empty());

  op.strength = 0.0f;
  EXPECT_TRUE(terrain::applyBrush(volume, op).empty());
}

TEST(TerrainBrushTest, InvalidBrushesAreRefused)
{
  const terrain::TerrainVolume volume = makeGround();
  const float nan = std::numeric_limits<float>::quiet_NaN();

  const auto valid = [](const Kind kind)
  {
    terrain::BrushOp op = brush(kind, craterCenter, 15.0f);
    op.planePoint = { 80.0f, 15.0f, 80.0f };
    return op;
  };

  const auto expectRefused = [&](const Kind kind, const auto& spoil)
  {
    const terrain::BrushOp control = valid(kind);
    EXPECT_FALSE(terrain::dirtyBox(volume, control).empty());
    EXPECT_FALSE(terrain::applyBrush(volume, control).empty());

    terrain::BrushOp op = control;
    spoil(op);
    EXPECT_TRUE(terrain::dirtyBox(volume, op).empty());
    EXPECT_TRUE(terrain::applyBrush(volume, op).empty());
  };

  expectRefused(Kind::add, [&](terrain::BrushOp& op) { op.center.x = nan; });
  expectRefused(Kind::add, [](terrain::BrushOp& op) { op.radius = 0.0f; });
  expectRefused(Kind::add, [](terrain::BrushOp& op) { op.radius = terrain::maxBrushRadiusVoxels * voxel + 1.0f; });
  expectRefused(Kind::add, [](terrain::BrushOp& op) { op.center = { 1.0e30f, 0.0f, 0.0f }; });
  expectRefused(Kind::add, [](terrain::BrushOp& op) { op.material = terrain::reservedMaterial; });
  expectRefused(Kind::add, [](terrain::BrushOp& op) { op.strength = 1.5f; });
  expectRefused(Kind::add, [](terrain::BrushOp& op) { op.falloff = -0.5f; });
  expectRefused(Kind::add, [](terrain::BrushOp& op) { op.falloff = 1.5f; });
  expectRefused(Kind::add, [](terrain::BrushOp& op) { op.strength = -0.1f; });
  expectRefused(Kind::add, [](terrain::BrushOp& op) { op.radius = std::numeric_limits<float>::infinity(); });
  expectRefused(Kind::add, [](terrain::BrushOp& op) { op.kind = static_cast<Kind>(9); });
  expectRefused(Kind::flatten, [](terrain::BrushOp& op) { op.planeNormal = { 0.0f, 0.0f, 0.0f }; });
}
