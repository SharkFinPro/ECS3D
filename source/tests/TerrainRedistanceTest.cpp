#include <gtest/gtest.h>

#include "terrain/TerrainChunk.h"
#include "terrain/TerrainGenerators.h"
#include "terrain/TerrainOps.h"
#include "terrain/TerrainSampling.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
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

  terrain::TerrainVolume flatGround()
  {
    terrain::TerrainVolume volume(voxel);
    terrain::commit(volume, terrain::fillFlat(volume, { 64, 64 }, 11.0f, 20.0f, 1));
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

  // Zero crossing of the vertical line through column (x, z), in voxels, scanning down from above the surface.
  float crossing(const terrain::TerrainVolume& volume, const int x, const int z)
  {
    terrain::Sampler sampler(volume);
    const auto at = [&](const float y)
    {
      return sampler.distance({ static_cast<float>(x) * voxel, y * voxel, static_cast<float>(z) * voxel });
    };

    for (float y = 20.0f; y > 0.0f; y -= 0.05f)
    {
      if (at(y) < 0.0f)
      {
        float high = y + 0.05f;
        float low = y;

        for (int i = 0; i < 40; ++i)
        {
          const float mid = 0.5f * (low + high);
          (at(mid) < 0.0f ? low : high) = mid;
        }

        return 0.5f * (low + high);
      }
    }

    return std::numeric_limits<float>::quiet_NaN();
  }

  void expectSurfaceAndSlope(terrain::TerrainVolume& volume)
  {
    flatten(volume, flattenBox);
    const terrain::TerrainVolume flattened = volume;

    const auto edits = terrain::redistance(volume, flattenBox);
    ASSERT_FALSE(edits.empty());
    terrain::commit(volume, edits);

    terrain::Sampler before(flattened);
    terrain::Sampler after(volume);
    int lines = 0;

    for (int x = 14; x <= 36; x += 4)
    {
      for (int z = 14; z <= 36; z += 4)
      {
        const float surfaceBefore = crossing(flattened, x, z);
        const float surfaceAfter = crossing(volume, x, z);
        ASSERT_FALSE(std::isnan(surfaceBefore));
        EXPECT_NEAR(surfaceBefore, surfaceAfter, 1e-3f) << x << "," << z;

        for (const float offset : { 2.0f, -2.0f })
        {
          const glm::vec3 point(static_cast<float>(x) * voxel, (surfaceBefore + offset) * voxel,
                                static_cast<float>(z) * voxel);
          EXPECT_NEAR(glm::length(before.gradient(point)) * voxel, 0.4f, 0.1f);
          EXPECT_NEAR(glm::length(after.gradient(point)) * voxel, 1.0f, 0.15f) << x << "," << z << "," << offset;
        }

        ++lines;
      }
    }

    EXPECT_EQ(lines, 36);

    int frozen = 0;

    for (int z = flattenBox.min.z; z < flattenBox.max.z; ++z)
    {
      for (int y = flattenBox.min.y; y < flattenBox.max.y; ++y)
      {
        for (int x = flattenBox.min.x; x < flattenBox.max.x; ++x)
        {
          const int16_t old = flattened.distanceAtSample({ x, y, z });
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

          EXPECT_EQ(isInside(old), isInside(volume.distanceAtSample({ x, y, z })));

          if (nearSurface)
          {
            EXPECT_EQ(old, volume.distanceAtSample({ x, y, z }));
            ++frozen;
          }
        }
      }
    }

    EXPECT_GT(frozen, 0);
  }
}

TEST(TerrainRedistanceTest, FlatGroundKeepsItsSurfaceAndRegainsUnitSlope)
{
  auto volume = flatGround();
  expectSurfaceAndSlope(volume);
}

TEST(TerrainRedistanceTest, RampKeepsItsSurfaceAndRegainsUnitSlope)
{
  auto volume = rampGround();
  expectSurfaceAndSlope(volume);
}

TEST(TerrainRedistanceTest, AnAlreadyDistanceFieldIsNearlyUnchanged)
{
  auto volume = flatGround();
  const terrain::TerrainVolume original = volume;
  terrain::commit(volume, terrain::redistance(volume, flattenBox));

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

  EXPECT_LT(terrain::decodeDistance(static_cast<int16_t>(maxStep)), 1e-3f);

  auto flattened = flatGround();
  flatten(flattened, flattenBox);
  EXPECT_FALSE(terrain::redistance(flattened, flattenBox).empty());
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
