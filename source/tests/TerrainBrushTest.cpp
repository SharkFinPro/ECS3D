#include <gtest/gtest.h>

#include "terrain/TerrainBrush.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainGenerators.h"
#include "terrain/TerrainOps.h"
#include "terrain/TerrainSampling.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <Protocol.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
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

  void apply(terrain::TerrainVolume& volume, const terrain::BrushOp& op)
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

  size_t countChangesOutside(const terrain::TerrainVolume& before, const terrain::TerrainVolume& after,
                             const terrain::ChunkEdits& edits, const terrain::SampleBox& box, size_t& changed)
  {
    size_t outside = 0;
    changed = 0;

    for (const auto& edit : edits)
    {
      const glm::ivec3 origin = terrain::chunkOrigin(edit.first);

      for (int z = origin.z; z < origin.z + terrain::chunkSize; ++z)
      {
        for (int y = origin.y; y < origin.y + terrain::chunkSize; ++y)
        {
          for (int x = origin.x; x < origin.x + terrain::chunkSize; ++x)
          {
            const glm::ivec3 sample(x, y, z);

            if (before.distanceAtSample(sample) == after.distanceAtSample(sample) &&
                before.materialAtSample(sample) == after.materialAtSample(sample))
            {
              continue;
            }

            ++changed;

            if (sample.x < box.min.x || sample.y < box.min.y || sample.z < box.min.z || sample.x >= box.max.x ||
                sample.y >= box.max.y || sample.z >= box.max.z)
            {
              ++outside;
            }
          }
        }
      }
    }

    return outside;
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

  net::Message packed(const terrain::BrushOp& op)
  {
    net::Message message(net::MessageType::snapshot);
    terrain::pack(message, op);
    return message;
  }
}

TEST(TerrainBrushTest, AddBuildsASphereIntoEmptySpace)
{
  terrain::TerrainVolume volume(voxel);
  const terrain::BrushOp op = brush(Kind::add, { 50.0f, 50.0f, 50.0f }, 25.0f);

  EXPECT_EQ(volume.chunks().size(), 0u);
  apply(volume, op);
  ASSERT_FALSE(volume.chunks().empty());

  terrain::Sampler sampler(volume);

  for (const glm::vec3 direction : { glm::vec3(1, 0, 0), glm::vec3(-1, 0, 0), glm::vec3(0, 1, 0),
                                     glm::vec3(0, -1, 0), glm::vec3(0, 0, 1), glm::vec3(0, 0, -1) })
  {
    EXPECT_NEAR(sampler.distance(op.center + direction * op.radius), 0.0f, 0.05f);
    EXPECT_LT(sampler.distance(op.center + direction * op.radius * 0.5f), -1.0f);
    EXPECT_GT(sampler.distance(op.center + direction * (op.radius + 3.0f * voxel)), 1.0f);
  }

  EXPECT_NEAR(sampler.distance(op.center), -terrain::bandVoxels, 1e-3f);
  EXPECT_EQ(volume.materialAtSample({ 20, 20, 20 }), 7);
}

TEST(TerrainBrushTest, SubtractCarvesACraterOutOfGround)
{
  terrain::TerrainVolume volume = makeGround();
  const glm::vec3 center(80.0f, 5.0f, 80.0f);

  {
    terrain::Sampler before(volume);
    EXPECT_LT(before.distance(center), -1.0f);
  }

  apply(volume, brush(Kind::subtract, center, 12.5f));

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
  apply(volume, op);

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
  apply(volume, op);

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
}

TEST(TerrainBrushTest, DirtyBoxContainsEveryChangedSample)
{
  for (const Kind kind : { Kind::add, Kind::subtract, Kind::smooth, Kind::flatten, Kind::paint })
  {
    const terrain::TerrainVolume volume = makeGround();
    const terrain::BrushOp op = sampleOp(kind);
    const terrain::ChunkEdits edits = terrain::applyBrush(volume, op);
    const terrain::SampleBox box = terrain::dirtyBox(volume, op);

    terrain::TerrainVolume after = volume;
    terrain::commit(after, edits);

    size_t changed = 0;
    EXPECT_EQ(countChangesOutside(volume, after, edits, box, changed), 0u) << "kind " << static_cast<int>(kind);
    EXPECT_GT(changed, 0u) << "kind " << static_cast<int>(kind);
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
  expectRefused(Kind::add, [](terrain::BrushOp& op) { op.kind = static_cast<Kind>(9); });
  expectRefused(Kind::flatten, [](terrain::BrushOp& op) { op.planeNormal = { 0.0f, 0.0f, 0.0f }; });
}

TEST(TerrainBrushTest, PackRoundTripsEveryKind)
{
  for (const Kind kind : { Kind::add, Kind::subtract, Kind::smooth, Kind::flatten, Kind::paint })
  {
    terrain::BrushOp op = sampleOp(kind);
    op.material = 12;

    const net::Message message = packed(op);
    net::MessageReader reader(message);
    const auto read = terrain::unpackBrushOp(reader);

    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->kind, op.kind);
    EXPECT_EQ(read->center, op.center);
    EXPECT_EQ(read->radius, op.radius);
    EXPECT_EQ(read->strength, op.strength);
    EXPECT_EQ(read->falloff, op.falloff);
    EXPECT_EQ(read->material, op.material);
    EXPECT_EQ(read->planePoint, op.planePoint);
    EXPECT_EQ(read->planeNormal, op.planeNormal);
    EXPECT_EQ(reader.remaining(), 0u);
  }
}

TEST(TerrainBrushTest, UnpackRefusesUntrustworthyFields)
{
  const auto refused = [](const auto& spoil)
  {
    terrain::BrushOp op = sampleOp(Kind::flatten);
    spoil(op);
    const net::Message message = packed(op);
    net::MessageReader reader(message);
    return !terrain::unpackBrushOp(reader).has_value();
  };

  EXPECT_FALSE(refused([](terrain::BrushOp&) {}));
  EXPECT_TRUE(refused([](terrain::BrushOp& op) { op.kind = static_cast<Kind>(9); }));
  EXPECT_TRUE(refused([](terrain::BrushOp& op) { op.radius = std::numeric_limits<float>::quiet_NaN(); }));
  EXPECT_TRUE(refused([](terrain::BrushOp& op) { op.strength = 1.5f; }));
  EXPECT_TRUE(refused([](terrain::BrushOp& op) { op.material = 255; }));
  EXPECT_TRUE(refused([](terrain::BrushOp& op) { op.radius = 0.0f; }));
}

TEST(TerrainBrushTest, UnpackThrowsOnATruncatedBuffer)
{
  const net::Message full = packed(sampleOp(Kind::add));
  const auto bytes = full.bytes();

  const net::Message whole(net::MessageType::snapshot, bytes);
  net::MessageReader wholeReader(whole);
  EXPECT_TRUE(terrain::unpackBrushOp(wholeReader).has_value());

  const net::Message cut(net::MessageType::snapshot, bytes.first(bytes.size() - 1));
  net::MessageReader cutReader(cut);
  EXPECT_THROW((void)terrain::unpackBrushOp(cutReader), std::runtime_error);
}
