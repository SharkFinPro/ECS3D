#include <gtest/gtest.h>

#include "terrain/TerrainChunk.h"
#include "terrain/TerrainCodec.h"
#include "terrain/TerrainTypes.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <random>
#include <span>
#include <vector>

namespace {
  void expectSameSamples(const terrain::TerrainChunk& expected, const terrain::TerrainChunk& actual)
  {
    EXPECT_EQ(expected.isUniform(), actual.isUniform());
    EXPECT_EQ(expected.hash(), actual.hash());

    for (int i = 0; i < terrain::samplesPerChunk; ++i)
    {
      ASSERT_EQ(expected.distanceAtIndex(i), actual.distanceAtIndex(i)) << i;
      ASSERT_EQ(expected.materialAtIndex(i), actual.materialAtIndex(i)) << i;
    }
  }

  std::shared_ptr<const terrain::TerrainChunk> planeChunk()
  {
    std::vector<int16_t> distances(terrain::samplesPerChunk);
    std::vector<uint8_t> materials(terrain::samplesPerChunk, 1);

    for (int z = 0; z < terrain::chunkSize; ++z)
    {
      for (int y = 0; y < terrain::chunkSize; ++y)
      {
        for (int x = 0; x < terrain::chunkSize; ++x)
        {
          distances[static_cast<size_t>(terrain::sampleIndex(x, y, z))] =
            terrain::encodeDistance(static_cast<float>(y) - 16.3f + 0.01f * static_cast<float>(x));
        }
      }
    }

    return terrain::TerrainChunk::dense(std::move(distances), std::move(materials));
  }

  std::shared_ptr<const terrain::TerrainChunk> randomChunk(const uint32_t seed)
  {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> distance(-terrain::airDistance, terrain::airDistance);
    std::uniform_int_distribution<int> material(0, 254);
    std::vector<int16_t> distances(terrain::samplesPerChunk);
    std::vector<uint8_t> materials(terrain::samplesPerChunk);

    for (int i = 0; i < terrain::samplesPerChunk; ++i)
    {
      distances[static_cast<size_t>(i)] = static_cast<int16_t>(distance(rng));
      materials[static_cast<size_t>(i)] = static_cast<uint8_t>(material(rng));
    }

    return terrain::TerrainChunk::dense(std::move(distances), std::move(materials));
  }

  // A valid dense encoding whose two arrays are each one repeat run of the given values.
  std::vector<uint8_t> flatDense(const uint16_t distance, const uint8_t materialByte)
  {
    std::vector<uint8_t> bytes{ 1 };
    // The first delta is the distance itself (low, high); the remaining 4095 deltas are zero.
    bytes.insert(bytes.end(), { 0, static_cast<uint8_t>(distance & 0xFF), static_cast<uint8_t>(distance >> 8) });
    int remaining = terrain::samplesPerChunk - 1;

    while (remaining > 0)
    {
      const int run = std::min(remaining, 130);
      bytes.insert(bytes.end(), { static_cast<uint8_t>(run + 125), 0, 0 });
      remaining -= run;
    }

    int left = terrain::samplesPerChunk;

    while (left > 0)
    {
      const int run = std::min(left, 130);
      bytes.insert(bytes.end(), { static_cast<uint8_t>(run + 125), materialByte });
      left -= run;
    }

    return bytes;
  }
}

TEST(TerrainCodecTest, UniformRoundTripsInFourBytes)
{
  const auto chunk = terrain::TerrainChunk::uniform(-12345, 17);
  const auto bytes = terrain::encodeChunk(*chunk);

  ASSERT_EQ(bytes.size(), 4u);
  EXPECT_EQ(bytes[0], 0);

  const auto decoded = terrain::decodeChunk(bytes);
  ASSERT_NE(decoded, nullptr);
  expectSameSamples(*chunk, *decoded);
}

TEST(TerrainCodecTest, UniformUsesLittleEndian)
{
  const auto bytes = terrain::encodeChunk(*terrain::TerrainChunk::uniform(0x1234, 9));

  ASSERT_EQ(bytes.size(), 4u);
  EXPECT_EQ(bytes[1], 0x34);
  EXPECT_EQ(bytes[2], 0x12);
  EXPECT_EQ(bytes[3], 9);
}

TEST(TerrainCodecTest, SmoothDenseRoundTripsAndCompresses)
{
  const auto chunk = planeChunk();
  ASSERT_FALSE(chunk->isUniform());

  const auto bytes = terrain::encodeChunk(*chunk);
  const auto decoded = terrain::decodeChunk(bytes);

  ASSERT_NE(decoded, nullptr);
  expectSameSamples(*chunk, *decoded);
  EXPECT_LT(bytes.size(), 16u * 1024u);
}

TEST(TerrainCodecTest, RandomDenseRoundTrips)
{
  for (const uint32_t seed : { 1u, 2u, 12345u })
  {
    const auto chunk = randomChunk(seed);
    const auto bytes = terrain::encodeChunk(*chunk);
    const auto decoded = terrain::decodeChunk(bytes);

    ASSERT_NE(decoded, nullptr) << seed;
    expectSameSamples(*chunk, *decoded);
  }
}

TEST(TerrainCodecTest, DistanceExtremesSurviveTheWraparoundDeltas)
{
  std::vector<int16_t> distances(terrain::samplesPerChunk);
  std::vector<uint8_t> materials(terrain::samplesPerChunk, 0);

  for (size_t i = 0; i < distances.size(); ++i)
  {
    distances[i] = (i % 2 == 0) ? terrain::airDistance : static_cast<int16_t>(-terrain::airDistance);
  }

  const auto chunk = terrain::TerrainChunk::dense(distances, materials);
  const auto decoded = terrain::decodeChunk(terrain::encodeChunk(*chunk));

  ASSERT_NE(decoded, nullptr);
  expectSameSamples(*chunk, *decoded);
}

TEST(TerrainCodecTest, EveryTruncationIsRejectedWhileTheFullEncodingDecodes)
{
  const auto bytes = terrain::encodeChunk(*planeChunk());

  ASSERT_NE(terrain::decodeChunk(bytes), nullptr);

  for (size_t length = 0; length < bytes.size(); ++length)
  {
    EXPECT_EQ(terrain::decodeChunk(std::span<const uint8_t>(bytes.data(), length)), nullptr) << length;
  }
}

TEST(TerrainCodecTest, EveryUniformTruncationIsRejected)
{
  const auto bytes = terrain::encodeChunk(*terrain::TerrainChunk::uniform(5, 5));

  ASSERT_NE(terrain::decodeChunk(bytes), nullptr);

  for (size_t length = 0; length < bytes.size(); ++length)
  {
    EXPECT_EQ(terrain::decodeChunk(std::span<const uint8_t>(bytes.data(), length)), nullptr) << length;
  }
}

TEST(TerrainCodecTest, RejectsEmptyInputAndUnknownTags)
{
  EXPECT_EQ(terrain::decodeChunk({}), nullptr);

  auto bytes = terrain::encodeChunk(*terrain::TerrainChunk::uniform(5, 5));
  ASSERT_NE(terrain::decodeChunk(bytes), nullptr);

  bytes[0] = 2;
  EXPECT_EQ(terrain::decodeChunk(bytes), nullptr);
  bytes[0] = 255;
  EXPECT_EQ(terrain::decodeChunk(bytes), nullptr);
}

TEST(TerrainCodecTest, RejectsTrailingBytes)
{
  auto uniform = terrain::encodeChunk(*terrain::TerrainChunk::uniform(5, 5));
  auto dense = terrain::encodeChunk(*planeChunk());
  ASSERT_NE(terrain::decodeChunk(uniform), nullptr);
  ASSERT_NE(terrain::decodeChunk(dense), nullptr);

  uniform.push_back(0);
  dense.push_back(0);

  EXPECT_EQ(terrain::decodeChunk(uniform), nullptr);
  EXPECT_EQ(terrain::decodeChunk(dense), nullptr);
}

TEST(TerrainCodecTest, RejectsARepeatRunThatOverflowsTheChunk)
{
  auto bytes = flatDense(10, 1);
  ASSERT_NE(terrain::decodeChunk(bytes), nullptr);

  // Replace the whole distance array by 4097 values: one literal, then 4096 more in repeat runs.
  std::vector<uint8_t> overflow{ 1, 0, 0, 0 };
  int count = 1;

  while (count < terrain::samplesPerChunk + 1)
  {
    const int run = std::min(130, terrain::samplesPerChunk + 1 - count);

    if (run < 3)
    {
      break;
    }

    overflow.insert(overflow.end(), { static_cast<uint8_t>(run + 125), 0, 0 });
    count += run;
  }

  EXPECT_EQ(terrain::decodeChunk(overflow), nullptr);

  // A single maximal repeat first, then enough to push past the end.
  std::vector<uint8_t> tooMany{ 1 };

  for (int i = 0; i < 33; ++i)
  {
    tooMany.insert(tooMany.end(), { 255, 0, 0 });
  }

  EXPECT_EQ(terrain::decodeChunk(tooMany), nullptr);
}

TEST(TerrainCodecTest, RejectsTooFewSamples)
{
  auto bytes = flatDense(10, 1);
  ASSERT_NE(terrain::decodeChunk(bytes), nullptr);

  // Drop the final material run packet: the material array now ends short.
  bytes.resize(bytes.size() - 2);
  EXPECT_EQ(terrain::decodeChunk(bytes), nullptr);
}

TEST(TerrainCodecTest, RejectsADistanceOutsideTheBand)
{
  EXPECT_NE(terrain::decodeChunk(flatDense(32764, 1)), nullptr);
  EXPECT_NE(terrain::decodeChunk(flatDense(0x8004, 1)), nullptr);

  EXPECT_EQ(terrain::decodeChunk(flatDense(32765, 1)), nullptr);
  EXPECT_EQ(terrain::decodeChunk(flatDense(0x8003, 1)), nullptr);
  EXPECT_EQ(terrain::decodeChunk(flatDense(0x8000, 1)), nullptr);

  const std::vector<uint8_t> uniformOutside{ 0, 0xFF, 0x7F, 1 };
  EXPECT_EQ(terrain::decodeChunk(uniformOutside), nullptr);

  const std::vector<uint8_t> uniformInside{ 0, 0xFC, 0x7F, 1 };
  EXPECT_NE(terrain::decodeChunk(uniformInside), nullptr);
}

TEST(TerrainCodecTest, RejectsTheReservedMaterial)
{
  EXPECT_NE(terrain::decodeChunk(flatDense(10, 254)), nullptr);
  EXPECT_EQ(terrain::decodeChunk(flatDense(10, 255)), nullptr);

  const std::vector<uint8_t> uniformReserved{ 0, 0, 0, 255 };
  const std::vector<uint8_t> uniformOk{ 0, 0, 0, 254 };
  EXPECT_EQ(terrain::decodeChunk(uniformReserved), nullptr);
  EXPECT_NE(terrain::decodeChunk(uniformOk), nullptr);
}
