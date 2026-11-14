#include <gtest/gtest.h>

#include "terrain/TerrainChunk.h"
#include "terrain/TerrainGenerators.h"
#include "terrain/TerrainOps.h"
#include "terrain/TerrainSampling.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <vector>

namespace {
  constexpr float voxel = 2.5f;
  const terrain::SampleBox flattenBox{ { 10, -4, 10 }, { 40, 24, 40 } };

  terrain::TerrainVolume flatGround(const float groundHeight = 11.0f)
  {
    terrain::TerrainVolume volume(voxel);
    terrain::commit(volume, terrain::fillFlat(volume, { 64, 64 }, groundHeight, 20.0f, 1));
    return volume;
  }

  terrain::TerrainVolume rampGround()
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
    terrain::commit(volume, terrain::fillFromHeightmap(volume, { pixels, 64, 64 }, 655.35f, 10.0f, 1));
    return volume;
  }

  const glm::vec3 sphereCenter(30.3f, 30.2f, 30.1f);
  constexpr float sphereRadius = 12.0f;

  terrain::TerrainVolume sphereGround()
  {
    terrain::TerrainVolume volume(voxel);
    const terrain::SampleBox fillBox{ { 0, 0, 0 }, { 60, 60, 60 } };
    terrain::commit(volume, terrain::editSamples(volume, fillBox,
                                                 [](const glm::ivec3 sample, int16_t& distance, uint8_t& material)
    {
      distance = terrain::encodeDistance(glm::length(glm::vec3(sample) - sphereCenter) - sphereRadius);
      material = 1;
    }));
    return volume;
  }

  void flatten(terrain::TerrainVolume& volume, const terrain::SampleBox& box)
  {
    const auto edits = terrain::editSamples(volume, box, [](glm::ivec3, int16_t& distance, uint8_t&)
    {
      distance = static_cast<int16_t>(std::lround(static_cast<float>(distance) * 0.4f));
    });
    ASSERT_FALSE(edits.empty());
    terrain::commit(volume, edits);
  }

  bool isInside(const int16_t distance)
  {
    return distance < 0;
  }

  // Distance t along the ray (in voxels) of the first zero crossing within maxT, or NaN.
  float crossing(const terrain::TerrainVolume& volume, const glm::vec3 origin, const glm::vec3 direction,
                 const float maxT)
  {
    terrain::Sampler sampler(volume);
    const auto at = [&](const float t)
    {
      return sampler.distance((origin + direction * t) * voxel);
    };

    for (float t = 0.05f; t < maxT; t += 0.05f)
    {
      if (at(t) < 0.0f)
      {
        float low = t - 0.05f;
        float high = t;

        for (int i = 0; i < 40; ++i)
        {
          const float mid = 0.5f * (low + high);
          (at(mid) < 0.0f ? high : low) = mid;
        }

        return 0.5f * (low + high);
      }
    }

    return std::numeric_limits<float>::quiet_NaN();
  }

  void expectRayKeepsSurfaceAndSlope(const terrain::TerrainVolume& flattened, const terrain::TerrainVolume& result,
                                     const glm::vec3 origin, const glm::vec3 direction, const float maxT,
                                     const float slopeTolerance)
  {
    const float surfaceBefore = crossing(flattened, origin, direction, maxT);
    const float surfaceAfter = crossing(result, origin, direction, maxT);
    ASSERT_FALSE(std::isnan(surfaceBefore));
    EXPECT_NEAR(surfaceBefore, surfaceAfter, 1e-3f);

    terrain::Sampler before(flattened);
    terrain::Sampler after(result);

    for (const float offset : { 2.0f, -2.0f })
    {
      const glm::vec3 point = (origin + direction * (surfaceBefore + offset)) * voxel;
      EXPECT_NEAR(glm::length(before.gradient(point)) * voxel, 0.4f, 0.1f);
      EXPECT_NEAR(glm::length(after.gradient(point)) * voxel, 1.0f, slopeTolerance);
    }
  }

  // Every sample in the chunks the box touches, including the margin and everything outside the box, keeps its sign;
  // samples outside the box are unchanged; samples next to the surface keep their exact value.
  void expectFrozenAndOutsideUnchanged(const terrain::TerrainVolume& flattened, const terrain::TerrainVolume& result,
                                       const terrain::SampleBox& box)
  {
    int frozen = 0;

    for (const auto coord : terrain::chunksTouched(box))
    {
      const glm::ivec3 origin = terrain::chunkOrigin(coord);

      for (int z = origin.z; z < origin.z + terrain::chunkSize; ++z)
      {
        for (int y = origin.y; y < origin.y + terrain::chunkSize; ++y)
        {
          for (int x = origin.x; x < origin.x + terrain::chunkSize; ++x)
          {
            const int16_t old = flattened.distanceAtSample({ x, y, z });
            const int16_t now = result.distanceAtSample({ x, y, z });
            EXPECT_EQ(isInside(old), isInside(now));

            const bool inBox = x >= box.min.x && x < box.max.x && y >= box.min.y && y < box.max.y &&
                               z >= box.min.z && z < box.max.z;

            if (!inBox)
            {
              EXPECT_EQ(old, now);
              continue;
            }

            bool nearSurface = old == 0;

            for (int dz = -1; dz <= 1; ++dz)
            {
              for (int dy = -1; dy <= 1; ++dy)
              {
                for (int dx = -1; dx <= 1; ++dx)
                {
                  nearSurface = nearSurface ||
                                isInside(flattened.distanceAtSample({ x + dx, y + dy, z + dz })) != isInside(old);
                }
              }
            }

            if (nearSurface)
            {
              EXPECT_EQ(old, now);
              ++frozen;
            }
          }
        }
      }
    }

    EXPECT_GT(frozen, 0);
  }

  void expectColumnsKeepSurfaceAndSlope(terrain::TerrainVolume& volume, const float slopeTolerance)
  {
    flatten(volume, flattenBox);
    const terrain::TerrainVolume flattened = volume;

    const auto edits = terrain::redistance(volume, flattenBox);
    ASSERT_FALSE(edits.empty());
    terrain::commit(volume, edits);

    for (int x = 14; x <= 36; x += 4)
    {
      for (int z = 14; z <= 36; z += 4)
      {
        expectRayKeepsSurfaceAndSlope(flattened, volume, { static_cast<float>(x), 20.0f, static_cast<float>(z) },
                                      { 0.0f, -1.0f, 0.0f }, 20.0f, slopeTolerance);
      }
    }

    expectFrozenAndOutsideUnchanged(flattened, volume, flattenBox);
  }
}

TEST(TerrainRedistanceTest, FlatGroundKeepsItsSurfaceAndRegainsUnitSlope)
{
  auto volume = flatGround();
  expectColumnsKeepSurfaceAndSlope(volume, 0.05f);
}

TEST(TerrainRedistanceTest, RampKeepsItsSurfaceAndRegainsUnitSlope)
{
  // The first free row beside the frozen 0.4-scaled samples is steeper than 1, so the ramp needs a looser bound.
  auto volume = rampGround();
  expectColumnsKeepSurfaceAndSlope(volume, 0.15f);
}

TEST(TerrainRedistanceTest, SphereKeepsItsSurfaceAndRegainsUnitSlope)
{
  auto volume = sphereGround();
  const terrain::SampleBox box{ { 5, 5, 5 }, { 55, 55, 55 } };
  flatten(volume, box);
  const terrain::TerrainVolume flattened = volume;

  const auto edits = terrain::redistance(volume, box);
  ASSERT_FALSE(edits.empty());
  terrain::commit(volume, edits);

  // Off-axis rays see the first-order Godunov error beside the frozen 0.4-scaled samples, up to about 0.18.
  const glm::vec3 directions[] = { { 1, 0, 0 },  { 0, 1, 0 },  { 1, 1, 0 },   { 1, 1, 1 },
                                   { 1, -2, 3 }, { -2, -1, 1 }, { -1, -3, -2 } };

  for (const glm::vec3 raw : directions)
  {
    const glm::vec3 direction = glm::normalize(raw);
    SCOPED_TRACE(::testing::Message() << direction.x << "," << direction.y << "," << direction.z);
    expectRayKeepsSurfaceAndSlope(flattened, volume, sphereCenter + direction * (sphereRadius + 3.0f), -direction,
                                  sphereRadius + 3.0f, 0.2f);
  }

  expectFrozenAndOutsideUnchanged(flattened, volume, box);
}

TEST(TerrainRedistanceTest, AnAlreadyDistanceFieldIsNearlyUnchanged)
{
  auto volume = flatGround();
  const terrain::TerrainVolume original = volume;
  const auto edits = terrain::redistance(volume, flattenBox);
  terrain::commit(volume, edits);

  int maxStep = 0;

  for (int z = flattenBox.min.z; z < flattenBox.max.z; ++z)
  {
    for (int y = flattenBox.min.y; y < flattenBox.max.y; ++y)
    {
      for (int x = flattenBox.min.x; x < flattenBox.max.x; ++x)
      {
        maxStep = std::max(maxStep, std::abs(volume.distanceAtSample({ x, y, z }) -
                                             original.distanceAtSample({ x, y, z })));
      }
    }
  }

  EXPECT_LE(maxStep, 2);
  EXPECT_TRUE(edits.empty());

  auto flattened = flatGround();
  flatten(flattened, flattenBox);
  EXPECT_FALSE(terrain::redistance(flattened, flattenBox).empty());
}

TEST(TerrainRedistanceTest, AnExactZeroSampleStaysZeroAndFreezesTheSignChange)
{
  auto volume = flatGround(10.0f);
  ASSERT_EQ(volume.distanceAtSample({ 20, 4, 20 }), 0);
  flatten(volume, flattenBox);
  const terrain::TerrainVolume flattened = volume;

  const auto edits = terrain::redistance(volume, flattenBox);
  ASSERT_FALSE(edits.empty());
  terrain::commit(volume, edits);

  for (const int y : { 3, 4 })
  {
    EXPECT_EQ(volume.distanceAtSample({ 20, y, 20 }), flattened.distanceAtSample({ 20, y, 20 })) << y;
  }

  EXPECT_EQ(volume.distanceAtSample({ 20, 4, 20 }), 0);
  EXPECT_EQ(volume.distanceAtSample({ 20, 5, 20 }), terrain::encodeDistance(1.0f));
  EXPECT_NE(volume.distanceAtSample({ 20, 5, 20 }), flattened.distanceAtSample({ 20, 5, 20 }));
}

TEST(TerrainRedistanceTest, WorksAcrossNegativeChunkCoordinates)
{
  terrain::TerrainVolume volume(voxel);
  const terrain::SampleBox fillBox{ { -40, -20, -40 }, { 10, 12, 10 } };
  terrain::commit(volume, terrain::editSamples(volume, fillBox,
                                               [](const glm::ivec3 sample, int16_t& distance, uint8_t& material)
  {
    distance = terrain::encodeDistance(static_cast<float>(sample.y) - 3.3f);
    material = 1;
  }));

  const terrain::SampleBox box{ { -30, -8, -30 }, { 0, 10, 0 } };
  flatten(volume, box);
  const int16_t frozenAbove = volume.distanceAtSample({ -10, 4, -10 });

  const auto edits = terrain::redistance(volume, box);
  ASSERT_FALSE(edits.empty());
  EXPECT_TRUE(std::ranges::any_of(edits, [](const auto& edit)
  {
    return edit.first.x < 0 && edit.first.z < 0;
  }));
  terrain::commit(volume, edits);

  EXPECT_EQ(volume.distanceAtSample({ -10, 4, -10 }), frozenAbove);
  EXPECT_NEAR(terrain::decodeDistance(volume.distanceAtSample({ -10, 6, -10 })),
              terrain::decodeDistance(frozenAbove) + 2.0f, 2e-3f);
}

TEST(TerrainRedistanceTest, RefusesEmptyAndOverCapBoxesButAcceptsAValidOne)
{
  auto volume = flatGround();
  flatten(volume, flattenBox);

  EXPECT_FALSE(terrain::redistance(volume, flattenBox).empty());
  EXPECT_TRUE(terrain::redistance(volume, { { 5, 5, 5 }, { 5, 9, 9 } }).empty());
  EXPECT_TRUE(terrain::redistance(volume, { { 0, 0, 0 }, { 32 * 200, 32, 32 * 100 } }).empty());
  EXPECT_TRUE(terrain::redistance(volume, { { 0, 0, 0 }, { 300, 300, 300 } }).empty());
}

TEST(TerrainRedistanceTest, AGrownGridExactlyAtTheCapIsAcceptedAndOneMoreIsRefused)
{
  static_assert(terrain::maxRedistanceSamples == 160ll * 160ll * 160ll);

  auto volume = flatGround();
  flatten(volume, flattenBox);

  const terrain::SampleBox atCap{ { 0, -70, 0 }, { 158, 88, 158 } };
  const terrain::SampleBox overCap{ { 0, -70, 0 }, { 159, 88, 158 } };

  EXPECT_FALSE(terrain::redistance(volume, atCap).empty());
  EXPECT_TRUE(terrain::redistance(volume, overCap).empty());
}

TEST(TerrainRedistanceTest, NeverMutatesTheInputVolume)
{
  auto volume = flatGround();
  flatten(volume, flattenBox);

  const auto before = volume.snapshotChunks();
  std::vector<uint64_t> hashes;

  for (const auto& entry : before)
  {
    hashes.push_back(entry.second->hash());
  }

  const auto edits = terrain::redistance(volume, flattenBox);
  ASSERT_FALSE(edits.empty());

  ASSERT_EQ(volume.chunks().size(), before.size());
  size_t index = 0;

  for (const auto& entry : before)
  {
    EXPECT_EQ(volume.chunk(entry.first), entry.second);
    EXPECT_EQ(entry.second->hash(), hashes[index++]);
  }
}
