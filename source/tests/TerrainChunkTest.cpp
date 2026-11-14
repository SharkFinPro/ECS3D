#include <gtest/gtest.h>

#include "ProjectFingerprint.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainTypes.h"

#include <glm/vec3.hpp>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
  std::vector<int16_t> filledDistances(const int16_t value)
  {
    return std::vector<int16_t>(terrain::samplesPerChunk, value);
  }

  std::vector<uint8_t> filledMaterials(const uint8_t value)
  {
    return std::vector<uint8_t>(terrain::samplesPerChunk, value);
  }
}

TEST(TerrainDistanceCodingTest, ClampsToTheBand)
{
  EXPECT_EQ(terrain::encodeDistance(100.0f), 32764);
  EXPECT_EQ(terrain::encodeDistance(-100.0f), -32764);
  EXPECT_EQ(terrain::encodeDistance(4.0f), terrain::airDistance);
  EXPECT_EQ(terrain::encodeDistance(std::numeric_limits<float>::infinity()), terrain::airDistance);
}

TEST(TerrainDistanceCodingTest, NegativeIsInsideAndZeroIsTheSurface)
{
  EXPECT_LT(terrain::encodeDistance(-0.5f), 0);
  EXPECT_GT(terrain::encodeDistance(0.5f), 0);
  EXPECT_EQ(terrain::encodeDistance(0.0f), 0);
}

TEST(TerrainDistanceCodingTest, RoundsToNearest)
{
  EXPECT_EQ(terrain::encodeDistance(1.0f), 8191);
  EXPECT_EQ(terrain::encodeDistance(-1.0f), -8191);
  EXPECT_EQ(terrain::encodeDistance(0.4f / 8191.0f), 0);
  EXPECT_EQ(terrain::encodeDistance(0.6f / 8191.0f), 1);
  EXPECT_EQ(terrain::encodeDistance(-0.6f / 8191.0f), -1);
}

TEST(TerrainDistanceCodingTest, NanEncodesAsAir)
{
  EXPECT_EQ(terrain::encodeDistance(std::numeric_limits<float>::quiet_NaN()), terrain::airDistance);
}

TEST(TerrainDistanceCodingTest, DecodeInvertsEncodeWithinHalfAStep)
{
  for (const float value : { -3.7f, -1.0f, -0.123f, 0.0f, 0.5f, 2.25f, 3.99f })
  {
    EXPECT_NEAR(terrain::decodeDistance(terrain::encodeDistance(value)), value, 0.5f / 8191.0f + 1.0e-6f);
  }

  EXPECT_FLOAT_EQ(terrain::decodeDistance(terrain::airDistance), terrain::bandVoxels);
}

TEST(TerrainChunkTest, UniformChunkReadsItsValueEverywhere)
{
  const auto chunk = terrain::TerrainChunk::uniform(-1234, 7);

  ASSERT_NE(chunk, nullptr);
  EXPECT_TRUE(chunk->isUniform());
  EXPECT_FALSE(chunk->isAir());
  EXPECT_EQ(chunk->distanceAt(0, 0, 0), -1234);
  EXPECT_EQ(chunk->distanceAt(31, 31, 31), -1234);
  EXPECT_EQ(chunk->materialAt(5, 6, 7), 7);
  EXPECT_EQ(chunk->distances().size(), 1u);
  EXPECT_EQ(chunk->materials().size(), 1u);
}

TEST(TerrainChunkTest, AirIsUniformAtTheAirDistanceWhateverTheMaterial)
{
  EXPECT_TRUE(terrain::TerrainChunk::uniform(terrain::airDistance, 0)->isAir());
  EXPECT_TRUE(terrain::TerrainChunk::uniform(terrain::airDistance, 9)->isAir());
  EXPECT_FALSE(terrain::TerrainChunk::uniform(terrain::airDistance - 1, 0)->isAir());
}

TEST(TerrainChunkTest, DenseRejectsWrongSizes)
{
  EXPECT_EQ(terrain::TerrainChunk::dense({}, {}), nullptr);
  EXPECT_EQ(terrain::TerrainChunk::dense(filledDistances(1), std::vector<uint8_t>(10, 0)), nullptr);
  EXPECT_EQ(terrain::TerrainChunk::dense(std::vector<int16_t>(10, 0), filledMaterials(0)), nullptr);
  EXPECT_EQ(terrain::TerrainChunk::dense(std::vector<int16_t>(terrain::samplesPerChunk + 1, 0), filledMaterials(0)),
            nullptr);
  EXPECT_NE(terrain::TerrainChunk::dense(filledDistances(1), filledMaterials(0)), nullptr);
}

TEST(TerrainChunkTest, UniformRejectsOutOfRangeValues)
{
  EXPECT_EQ(terrain::TerrainChunk::uniform(terrain::airDistance + 1, 0), nullptr);
  EXPECT_EQ(terrain::TerrainChunk::uniform(-terrain::airDistance - 1, 0), nullptr);
  EXPECT_EQ(terrain::TerrainChunk::uniform(std::numeric_limits<int16_t>::min(), 0), nullptr);
  EXPECT_EQ(terrain::TerrainChunk::uniform(0, terrain::reservedMaterial), nullptr);

  EXPECT_NE(terrain::TerrainChunk::uniform(terrain::airDistance, 0), nullptr);
  EXPECT_NE(terrain::TerrainChunk::uniform(-terrain::airDistance, 254), nullptr);
}

TEST(TerrainChunkTest, DenseRejectsOutOfRangeValuesAnywhere)
{
  for (const size_t at : { size_t{ 0 }, size_t{ 2000 }, static_cast<size_t>(terrain::samplesPerChunk - 1) })
  {
    auto distances = filledDistances(5);
    distances[at] = terrain::airDistance + 1;
    EXPECT_EQ(terrain::TerrainChunk::dense(distances, filledMaterials(1)), nullptr) << at;

    distances[at] = -terrain::airDistance - 1;
    EXPECT_EQ(terrain::TerrainChunk::dense(distances, filledMaterials(1)), nullptr) << at;

    auto materials = filledMaterials(1);
    materials[at] = terrain::reservedMaterial;
    EXPECT_EQ(terrain::TerrainChunk::dense(filledDistances(5), materials), nullptr) << at;

    distances[at] = terrain::airDistance;
    materials[at] = 254;
    EXPECT_NE(terrain::TerrainChunk::dense(distances, materials), nullptr) << at;
  }
}

TEST(TerrainChunkTest, DenseCollapsesToUniformWhenEverySampleIsEqual)
{
  const auto chunk = terrain::TerrainChunk::dense(filledDistances(-500), filledMaterials(3));

  ASSERT_NE(chunk, nullptr);
  EXPECT_TRUE(chunk->isUniform());
  EXPECT_EQ(chunk->distanceAt(10, 10, 10), -500);
  EXPECT_EQ(chunk->materialAt(10, 10, 10), 3);
}

TEST(TerrainChunkTest, OneDifferingDistanceOrMaterialStaysDense)
{
  auto distances = filledDistances(-500);
  distances[1234] = 17;
  const auto byDistance = terrain::TerrainChunk::dense(distances, filledMaterials(3));

  ASSERT_NE(byDistance, nullptr);
  EXPECT_FALSE(byDistance->isUniform());
  EXPECT_EQ(byDistance->distances().size(), static_cast<size_t>(terrain::samplesPerChunk));

  auto materials = filledMaterials(3);
  materials[4000] = 4;
  const auto byMaterial = terrain::TerrainChunk::dense(filledDistances(-500), materials);

  ASSERT_NE(byMaterial, nullptr);
  EXPECT_FALSE(byMaterial->isUniform());
  EXPECT_EQ(byMaterial->materialAtIndex(4000), 4);
}

TEST(TerrainChunkTest, IndexOrderIsXThenYThenZ)
{
  std::vector<int16_t> distances(terrain::samplesPerChunk);
  std::vector<uint8_t> materials(terrain::samplesPerChunk);

  for (int i = 0; i < terrain::samplesPerChunk; ++i)
  {
    distances[static_cast<size_t>(i)] = static_cast<int16_t>(i - 16000);
    materials[static_cast<size_t>(i)] = static_cast<uint8_t>(i % 200);
  }

  const auto chunk = terrain::TerrainChunk::dense(distances, materials);

  ASSERT_NE(chunk, nullptr);
  EXPECT_EQ(chunk->distanceAt(0, 0, 0), -16000);
  EXPECT_EQ(chunk->distanceAt(1, 0, 0), -15999);
  EXPECT_EQ(chunk->distanceAt(0, 1, 0), -16000 + 32);
  EXPECT_EQ(chunk->distanceAt(0, 0, 1), -16000 + 1024);
  EXPECT_EQ(chunk->distanceAt(3, 4, 5), -16000 + 3 + 32 * (4 + 32 * 5));
  EXPECT_EQ(chunk->materialAt(3, 4, 5), (3 + 32 * (4 + 32 * 5)) % 200);
  EXPECT_EQ(terrain::sampleIndex(3, 4, 5), 3 + 32 * (4 + 32 * 5));
}

TEST(TerrainChunkTest, EqualContentHashesEqualWhicheverFactoryBuiltIt)
{
  const auto viaUniform = terrain::TerrainChunk::uniform(-800, 2);
  const auto viaDense = terrain::TerrainChunk::dense(filledDistances(-800), filledMaterials(2));

  ASSERT_NE(viaDense, nullptr);
  EXPECT_EQ(viaUniform->hash(), viaDense->hash());

  auto distances = filledDistances(-800);
  distances[77] = 5;
  const auto first = terrain::TerrainChunk::dense(distances, filledMaterials(2));
  const auto second = terrain::TerrainChunk::dense(distances, filledMaterials(2));

  EXPECT_EQ(first->hash(), second->hash());
}

TEST(TerrainChunkTest, HashMatchesFingerprintBytesOfTheCanonicalForm)
{
  std::vector<int16_t> distances(terrain::samplesPerChunk);
  std::vector<uint8_t> materials(terrain::samplesPerChunk);

  for (int i = 0; i < terrain::samplesPerChunk; ++i)
  {
    distances[static_cast<size_t>(i)] = static_cast<int16_t>((i * 37) % 60000 - 30000);
    materials[static_cast<size_t>(i)] = static_cast<uint8_t>(i % 251);
  }

  std::string canonical(1, '\x01');

  for (const int16_t distance : distances)
  {
    const auto raw = static_cast<uint16_t>(distance);
    canonical.push_back(static_cast<char>(raw & 0xFF));
    canonical.push_back(static_cast<char>(raw >> 8));
  }

  for (const uint8_t material : materials)
  {
    canonical.push_back(static_cast<char>(material));
  }

  const auto chunk = terrain::TerrainChunk::dense(distances, materials);

  ASSERT_NE(chunk, nullptr);
  EXPECT_EQ(chunk->hash(), fingerprintBytes(canonical));

  const std::string uniformCanonical{ '\x00', '\x0A', '\x00', '\x03' };
  EXPECT_EQ(terrain::TerrainChunk::uniform(10, 3)->hash(), fingerprintBytes(uniformCanonical));
}

TEST(TerrainChunkTest, DifferentContentHashesDifferently)
{
  const auto base = terrain::TerrainChunk::uniform(-800, 2);

  EXPECT_NE(base->hash(), terrain::TerrainChunk::uniform(-801, 2)->hash());
  EXPECT_NE(base->hash(), terrain::TerrainChunk::uniform(-800, 3)->hash());

  auto distances = filledDistances(-800);
  distances[77] = 5;
  const auto changed = terrain::TerrainChunk::dense(distances, filledMaterials(2));
  distances[77] = 6;
  const auto changedAgain = terrain::TerrainChunk::dense(distances, filledMaterials(2));

  EXPECT_NE(base->hash(), changed->hash());
  EXPECT_NE(changed->hash(), changedAgain->hash());
}

TEST(TerrainChunkTest, ChangingTheLastDistanceOrOneMaterialChangesTheHash)
{
  auto distances = filledDistances(-800);
  distances[0] = 3;
  const auto base = terrain::TerrainChunk::dense(distances, filledMaterials(2));

  auto lastChanged = distances;
  lastChanged.back() = 4;
  const auto byLastDistance = terrain::TerrainChunk::dense(lastChanged, filledMaterials(2));

  auto materials = filledMaterials(2);
  materials[1500] = 3;
  const auto byMaterial = terrain::TerrainChunk::dense(distances, materials);

  ASSERT_NE(byLastDistance, nullptr);
  ASSERT_NE(byMaterial, nullptr);
  EXPECT_NE(base->hash(), byLastDistance->hash());
  EXPECT_NE(base->hash(), byMaterial->hash());
  EXPECT_NE(byLastDistance->hash(), byMaterial->hash());
  EXPECT_EQ(base->hash(), terrain::TerrainChunk::dense(distances, filledMaterials(2))->hash());
}

TEST(TerrainChunkTest, DenseByteSizeDwarfsUniform)
{
  auto distances = filledDistances(-800);
  distances[0] = 1;
  const auto dense = terrain::TerrainChunk::dense(distances, filledMaterials(2));
  const auto uniform = terrain::TerrainChunk::uniform(-800, 2);

  EXPECT_GE(dense->byteSize(), static_cast<size_t>(terrain::samplesPerChunk) * 3);
  EXPECT_LT(uniform->byteSize(), 512u);
  EXPECT_GT(dense->byteSize(), uniform->byteSize() * 10);
}

TEST(TerrainTypesTest, FloorDivRoundsTowardNegativeInfinity)
{
  EXPECT_EQ(terrain::floorDiv(0, 32), 0);
  EXPECT_EQ(terrain::floorDiv(31, 32), 0);
  EXPECT_EQ(terrain::floorDiv(32, 32), 1);
  EXPECT_EQ(terrain::floorDiv(-1, 32), -1);
  EXPECT_EQ(terrain::floorDiv(-32, 32), -1);
  EXPECT_EQ(terrain::floorDiv(-33, 32), -2);
}

TEST(TerrainTypesTest, ChunkAndLocalIndexOnBordersAndNegatives)
{
  struct Case
  {
    int sample;
    int chunk;
    int local;
  };

  for (const Case c : { Case{ -33, -2, 31 }, Case{ -32, -1, 0 }, Case{ -1, -1, 31 }, Case{ 0, 0, 0 },
                        Case{ 31, 0, 31 }, Case{ 32, 1, 0 } })
  {
    const glm::ivec3 sample(c.sample, c.sample, c.sample);
    const terrain::ChunkCoord coord = terrain::chunkOfSample(sample);

    EXPECT_EQ(coord.x, c.chunk) << c.sample;
    EXPECT_EQ(coord.y, c.chunk) << c.sample;
    EXPECT_EQ(coord.z, c.chunk) << c.sample;
    EXPECT_EQ(terrain::indexInChunk(sample), terrain::sampleIndex(c.local, c.local, c.local)) << c.sample;
  }

  const glm::ivec3 origin = terrain::chunkOrigin({ -2, 0, 1 });
  EXPECT_EQ(origin, glm::ivec3(-64, 0, 32));
  EXPECT_EQ(terrain::indexInChunk(origin), 0);
}

TEST(TerrainTypesTest, ChunksTouchedInsideOneChunk)
{
  const auto touched = terrain::chunksTouched({ { 1, 2, 3 }, { 30, 31, 32 } });

  ASSERT_EQ(touched.size(), 1u);
  EXPECT_EQ(touched.front(), (terrain::ChunkCoord{ 0, 0, 0 }));
}

TEST(TerrainTypesTest, ChunksTouchedStraddlingBorders)
{
  const auto touched = terrain::chunksTouched({ { 31, 0, 0 }, { 33, 1, 1 } });

  ASSERT_EQ(touched.size(), 2u);
  EXPECT_EQ(touched[0], (terrain::ChunkCoord{ 0, 0, 0 }));
  EXPECT_EQ(touched[1], (terrain::ChunkCoord{ 1, 0, 0 }));

  EXPECT_EQ(terrain::chunksTouched({ { 31, 31, 31 }, { 33, 33, 33 } }).size(), 8u);
}

TEST(TerrainTypesTest, ChunksTouchedNegativeAndExclusiveMax)
{
  const auto touched = terrain::chunksTouched({ { -33, 0, 0 }, { -31, 1, 1 } });

  ASSERT_EQ(touched.size(), 2u);
  EXPECT_EQ(touched[0], (terrain::ChunkCoord{ -2, 0, 0 }));
  EXPECT_EQ(touched[1], (terrain::ChunkCoord{ -1, 0, 0 }));

  const auto exact = terrain::chunksTouched({ { -32, 0, 0 }, { 0, 32, 32 } });
  ASSERT_EQ(exact.size(), 1u);
  EXPECT_EQ(exact.front(), (terrain::ChunkCoord{ -1, 0, 0 }));
}

TEST(TerrainTypesTest, EmptyBoxTouchesNothing)
{
  const terrain::SampleBox flat{ { 0, 0, 0 }, { 5, 0, 5 } };
  const terrain::SampleBox inverted{ { 5, 5, 5 }, { 0, 9, 9 } };
  const terrain::SampleBox solid{ { 0, 0, 0 }, { 5, 5, 5 } };

  EXPECT_TRUE(flat.empty());
  EXPECT_TRUE(inverted.empty());
  EXPECT_TRUE(terrain::chunksTouched(flat).empty());
  EXPECT_TRUE(terrain::chunksTouched(inverted).empty());
  EXPECT_FALSE(solid.empty());
  EXPECT_EQ(terrain::chunksTouched(solid).size(), 1u);
}

TEST(TerrainTypesTest, ChunkCoordHashWorksInAnUnorderedMapWithNegativeKeys)
{
  std::unordered_map<terrain::ChunkCoord, int> map;
  int next = 0;

  for (int x = -3; x <= 3; ++x)
  {
    for (int y = -3; y <= 3; ++y)
    {
      for (int z = -3; z <= 3; ++z)
      {
        map[{ x, y, z }] = next++;
      }
    }
  }

  EXPECT_EQ(map.size(), 343u);
  EXPECT_EQ((map.at({ -3, -3, -3 })), 0);
  EXPECT_EQ((map.at({ 3, 3, 3 })), 342);
  EXPECT_EQ(map.count({ 4, 0, 0 }), 0u);

  std::unordered_set<size_t> hashes;

  for (const auto& entry : map)
  {
    hashes.insert(std::hash<terrain::ChunkCoord>{}(entry.first));
  }

  EXPECT_EQ(hashes.size(), map.size());
}
