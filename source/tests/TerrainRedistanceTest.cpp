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
#include <cstdlib>
#include <iterator>
#include <limits>
#include <memory>
#include <tuple>
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

  bool contains(const terrain::SampleBox& box, const glm::ivec3 sample)
  {
    return glm::all(glm::greaterThanEqual(sample, box.min)) && glm::all(glm::lessThan(sample, box.max));
  }

  bool touchesSurface(const terrain::TerrainVolume& volume, const glm::ivec3 sample)
  {
    const int16_t distance = volume.distanceAtSample(sample);
    bool touches = distance == 0;

    for (int dz = -1; dz <= 1; ++dz)
    {
      for (int dy = -1; dy <= 1; ++dy)
      {
        for (int dx = -1; dx <= 1; ++dx)
        {
          touches = touches || isInside(volume.distanceAtSample(sample + glm::ivec3(dx, dy, dz))) != isInside(distance);
        }
      }
    }

    return touches;
  }

  // Returns whether the sample is one redistance must hold fixed.
  bool expectSampleKept(const terrain::TerrainVolume& flattened, const terrain::TerrainVolume& result,
                        const terrain::SampleBox& box, const glm::ivec3 sample)
  {
    const int16_t old = flattened.distanceAtSample(sample);
    const int16_t now = result.distanceAtSample(sample);
    EXPECT_EQ(isInside(old), isInside(now));

    const bool held = !contains(box, sample) || touchesSurface(flattened, sample);

    if (held)
    {
      EXPECT_EQ(old, now);
    }

    return held && contains(box, sample);
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
            frozen += expectSampleKept(flattened, result, box, { x, y, z }) ? 1 : 0;
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

namespace {
  // The Godunov upwind update, written out independently of TerrainOps.
  float referenceUpwind(float a, float b, float c)
  {
    float sorted[] = { a, b, c };
    std::sort(std::begin(sorted), std::end(sorted));
    a = sorted[0];
    b = sorted[1];
    c = sorted[2];

    float u = a + 1.0f;

    if (u > b)
    {
      u = 0.5f * (a + b + std::sqrt(std::max(0.0f, 2.0f - (a - b) * (a - b))));

      if (u > c)
      {
        const float sum = a + b + c;
        const float squares = a * a + b * b + c * c;
        u = (sum + std::sqrt(std::max(0.0f, sum * sum - 3.0f * (squares - 1.0f)))) / 3.0f;
      }
    }

    return u;
  }

  // Flat ground with a steep dig and a blob, whose distances are no longer distance-like.
  terrain::TerrainVolume dugGround()
  {
    terrain::TerrainVolume volume(1.0f);
    terrain::commit(volume, terrain::fillFlat(volume, { 96, 96 }, 20.0f, 20.0f, 1));

    const std::tuple<terrain::BrushOp::Kind, glm::vec3, float> shapes[] = {
      { terrain::BrushOp::Kind::subtract, glm::vec3(40.0f, 20.0f, 40.0f), 12.0f },
      { terrain::BrushOp::Kind::add, glm::vec3(60.0f, 24.0f, 50.0f), 8.0f }
    };

    for (const auto& [kind, center, radius] : shapes)
    {
      terrain::BrushOp op;
      op.kind = kind;
      op.center = center;
      op.radius = radius;
      op.strength = 1.0f;
      op.falloff = 0.0f;
      op.material = 1;
      terrain::commit(volume, terrain::applyBrush(volume, op));
    }

    return volume;
  }

  // A plain redistance of box: 27-neighbor freeze, then full-grid Jacobi passes to convergence with no skipping.
  class ReferenceRedistance
  {
  public:
    ReferenceRedistance(const terrain::TerrainVolume& volume, const terrain::SampleBox& box)
      : m_min(box.min - glm::ivec3(1)), m_size(box.max - box.min + glm::ivec3(2)),
        m_encoded(count()), m_field(count()), m_fixed(count(), 0)
    {
      forEach([&](const glm::ivec3 local)
      {
        m_encoded[at(local)] = volume.distanceAtSample(m_min + local);
        m_field[at(local)] = terrain::decodeDistance(m_encoded[at(local)]);
      });

      forEach([&](const glm::ivec3 local) { freeze(local); });

      while (pass())
      {
      }
    }

    [[nodiscard]] size_t samples() const
    {
      return count();
    }

    // The encoded distance redistance should leave at sample.
    [[nodiscard]] int expectedAt(const glm::ivec3 sample) const
    {
      const size_t index = at(sample - m_min);
      return m_fixed[index] != 0 ? m_encoded[index] : terrain::encodeDistance(m_field[index]);
    }

  private:
    glm::ivec3 m_min;
    glm::ivec3 m_size;
    std::vector<int16_t> m_encoded;
    std::vector<float> m_field;
    std::vector<uint8_t> m_fixed;

    [[nodiscard]] size_t count() const
    {
      return static_cast<size_t>(m_size.x) * static_cast<size_t>(m_size.y) * static_cast<size_t>(m_size.z);
    }

    [[nodiscard]] size_t at(const glm::ivec3 local) const
    {
      return static_cast<size_t>(local.x + m_size.x * (local.y + m_size.y * local.z));
    }

    [[nodiscard]] bool onBorder(const glm::ivec3 local) const
    {
      return local.x == 0 || local.y == 0 || local.z == 0 || local.x == m_size.x - 1 || local.y == m_size.y - 1 ||
             local.z == m_size.z - 1;
    }

    template <typename Visit>
    void forEach(const Visit& visit) const
    {
      for (int z = 0; z < m_size.z; ++z)
      {
        for (int y = 0; y < m_size.y; ++y)
        {
          for (int x = 0; x < m_size.x; ++x)
          {
            visit(glm::ivec3(x, y, z));
          }
        }
      }
    }

    [[nodiscard]] bool signChangesAround(const glm::ivec3 local) const
    {
      const bool inside = m_encoded[at(local)] < 0;

      for (int dz = -1; dz <= 1; ++dz)
      {
        for (int dy = -1; dy <= 1; ++dy)
        {
          for (int dx = -1; dx <= 1; ++dx)
          {
            if ((m_encoded[at(local + glm::ivec3(dx, dy, dz))] < 0) != inside)
            {
              return true;
            }
          }
        }
      }

      return false;
    }

    void freeze(const glm::ivec3 local)
    {
      const size_t index = at(local);
      const bool frozen = onBorder(local) || m_encoded[index] == 0 || signChangesAround(local);

      m_fixed[index] = frozen ? 1 : 0;

      if (!frozen)
      {
        m_field[index] = m_encoded[index] < 0 ? -terrain::bandVoxels : terrain::bandVoxels;
      }
    }

    [[nodiscard]] float nearest(const glm::ivec3 local, const glm::ivec3 step) const
    {
      return std::min(std::abs(m_field[at(local - step)]), std::abs(m_field[at(local + step)]));
    }

    // One Jacobi pass; whether any sample changed.
    bool pass()
    {
      std::vector<float> next = m_field;
      bool changed = false;

      forEach([&](const glm::ivec3 local)
      {
        const size_t index = at(local);

        if (onBorder(local) || m_fixed[index] != 0)
        {
          return;
        }

        const float current = m_field[index];
        const float upwind = referenceUpwind(nearest(local, { 1, 0, 0 }), nearest(local, { 0, 1, 0 }),
                                             nearest(local, { 0, 0, 1 }));
        const float magnitude = std::min(std::abs(current), upwind);
        next[index] = std::signbit(current) ? -magnitude : magnitude;
        changed = changed || next[index] != current;
      });

      m_field.swap(next);
      return changed;
    }
  };

  // The largest difference, in encoded units, between the volume and the reference over box.
  int worstDifference(const terrain::TerrainVolume& volume, const ReferenceRedistance& reference,
                      const terrain::SampleBox& box)
  {
    int worst = 0;

    for (int z = box.min.z; z < box.max.z; ++z)
    {
      for (int y = box.min.y; y < box.max.y; ++y)
      {
        for (int x = box.min.x; x < box.max.x; ++x)
        {
          const glm::ivec3 sample(x, y, z);
          worst = std::max(worst, std::abs(volume.distanceAtSample(sample) - reference.expectedAt(sample)));
        }
      }
    }

    return worst;
  }
}

// On a grid large enough for the parallel passes, redistance must agree everywhere in the box with a plain reference.
TEST(TerrainRedistanceTest, MatchesAFullJacobiReferenceOnALargeBox)
{
  const terrain::TerrainVolume volume = dugGround();
  const terrain::SampleBox box{ { 4, 0, 4 }, { 84, 44, 84 } };
  const ReferenceRedistance reference(volume, box);
  ASSERT_GT(reference.samples(), 65536u);

  terrain::TerrainVolume redistanced(1.0f);
  redistanced.restoreChunks(volume.snapshotChunks());
  const auto edits = terrain::redistance(volume, box);
  ASSERT_FALSE(edits.empty());
  terrain::commit(redistanced, edits);

  // A unit or two of the encoding allows for floating-point contraction differing between this file and TerrainOps.
  EXPECT_LE(worstDifference(redistanced, reference, box), 2);
}
