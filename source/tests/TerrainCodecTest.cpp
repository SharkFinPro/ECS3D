#include <gtest/gtest.h>

#include "terrain/TerrainChunk.h"
#include "terrain/TerrainCodec.h"
#include "terrain/TerrainTypes.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <cstddef>
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

  using Bytes = std::vector<uint8_t>;

  constexpr size_t samples = static_cast<size_t>(terrain::samplesPerChunk);

  void putValue(Bytes& out, const uint16_t value, const int width)
  {
    out.push_back(static_cast<uint8_t>(value & 0xFF));

    if (width == 2)
    {
      out.push_back(static_cast<uint8_t>(value >> 8));
    }
  }

  // Appends repeat packets that together repeat the value count times.
  void appendRepeats(Bytes& out, const size_t count, const uint16_t value, const int width)
  {
    size_t left = count;

    while (left > 0)
    {
      const size_t run = std::min<size_t>(left, 130);
      out.push_back(static_cast<uint8_t>(run + 125));
      putValue(out, value, width);
      left -= run;
    }
  }

  // Appends one literal packet holding the values.
  void appendLiteral(Bytes& out, const std::vector<uint16_t>& values, const int width)
  {
    out.push_back(static_cast<uint8_t>(values.size() - 1));

    for (const uint16_t value : values)
    {
      putValue(out, value, width);
    }
  }

  Bytes denseStream(const Bytes& distancePackets, const Bytes& materialPackets)
  {
    Bytes bytes{ 1 };
    bytes.insert(bytes.end(), distancePackets.begin(), distancePackets.end());
    bytes.insert(bytes.end(), materialPackets.begin(), materialPackets.end());
    return bytes;
  }

  // Distance packets for an array of zeros: every delta is zero.
  Bytes zeroDistances()
  {
    Bytes packets;
    appendRepeats(packets, samples, 0, 2);
    return packets;
  }

  Bytes materialsOf(const uint8_t material)
  {
    Bytes packets;
    appendRepeats(packets, samples, material, 1);
    return packets;
  }

  // Delta codes the distances and writes both arrays as literal packets only: valid but not canonical.
  Bytes literalStream(const std::vector<uint16_t>& distances, const std::vector<uint8_t>& materials)
  {
    Bytes bytes{ 1 };
    uint16_t previous = 0;

    for (size_t start = 0; start < samples; start += 128)
    {
      std::vector<uint16_t> deltas;

      for (size_t i = start; i < start + 128; ++i)
      {
        deltas.push_back(static_cast<uint16_t>(distances[i] - previous));
        previous = distances[i];
      }

      appendLiteral(bytes, deltas, 2);
    }

    for (size_t start = 0; start < samples; start += 128)
    {
      const auto first = materials.begin() + static_cast<std::ptrdiff_t>(start);
      appendLiteral(bytes, std::vector<uint16_t>(first, first + 128), 1);
    }

    return bytes;
  }

  struct RawSamples
  {
    std::vector<uint16_t> distances;
    std::vector<uint8_t> materials;
  };

  // A valid, non-uniform sample set.
  RawSamples validSamples()
  {
    RawSamples raw;

    for (size_t i = 0; i < samples; ++i)
    {
      raw.distances.push_back(static_cast<uint16_t>(static_cast<int16_t>(static_cast<int>(i % 200) - 100)));
      raw.materials.push_back(static_cast<uint8_t>(i % 50));
    }

    return raw;
  }
}

TEST(TerrainCodecTest, UniformRoundTripsInFourBytes)
{
  const auto chunk = terrain::TerrainChunk::uniform(-12345, 17);
  const auto bytes = terrain::encodeChunk(*chunk);

  ASSERT_EQ(bytes.size(), 4u);
  EXPECT_EQ(static_cast<int>(bytes[0]), 0);

  const auto decoded = terrain::decodeChunk(bytes);
  ASSERT_NE(decoded, nullptr);
  expectSameSamples(*chunk, *decoded);
}

TEST(TerrainCodecTest, UniformUsesLittleEndian)
{
  const auto bytes = terrain::encodeChunk(*terrain::TerrainChunk::uniform(0x1234, 9));

  ASSERT_EQ(bytes.size(), 4u);
  EXPECT_EQ(static_cast<int>(bytes[1]), 0x34);
  EXPECT_EQ(static_cast<int>(bytes[2]), 0x12);
  EXPECT_EQ(static_cast<int>(bytes[3]), 9);
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

TEST(TerrainCodecTest, RejectsDistanceRepeatsThatOverflowTheChunk)
{
  // Positive control: exactly samplesPerChunk distances decode.
  EXPECT_NE(terrain::decodeChunk(denseStream(zeroDistances(), materialsOf(1))), nullptr);

  for (const size_t count : { samples + 3, samples + 100, samples + 1000 })
  {
    Bytes distances;
    appendRepeats(distances, count, 0, 2);

    EXPECT_EQ(terrain::decodeChunk(denseStream(distances, materialsOf(1))), nullptr) << count;
  }
}

TEST(TerrainCodecTest, RejectsALiteralDistancePacketThatOverflowsTheChunk)
{
  const auto build = [](const size_t repeated)
  {
    Bytes distances;
    appendRepeats(distances, repeated, 0, 2);
    appendLiteral(distances, { 0, 0 }, 2);
    return denseStream(distances, materialsOf(1));
  };

  EXPECT_NE(terrain::decodeChunk(build(samples - 2)), nullptr);
  EXPECT_EQ(terrain::decodeChunk(build(samples - 1)), nullptr);
}

TEST(TerrainCodecTest, RejectsMaterialRepeatsThatOverflowTheChunk)
{
  EXPECT_NE(terrain::decodeChunk(denseStream(zeroDistances(), materialsOf(1))), nullptr);

  for (const size_t count : { samples + 3, samples + 100, samples + 1000 })
  {
    Bytes materials;
    appendRepeats(materials, count, 1, 1);

    EXPECT_EQ(terrain::decodeChunk(denseStream(zeroDistances(), materials)), nullptr) << count;
  }
}

TEST(TerrainCodecTest, RejectsALiteralMaterialPacketThatOverflowsTheChunk)
{
  const auto build = [](const size_t repeated)
  {
    Bytes materials;
    appendRepeats(materials, repeated, 1, 1);
    appendLiteral(materials, { 1, 1 }, 1);
    return denseStream(zeroDistances(), materials);
  };

  EXPECT_NE(terrain::decodeChunk(build(samples - 2)), nullptr);
  EXPECT_EQ(terrain::decodeChunk(build(samples - 1)), nullptr);
}

TEST(TerrainCodecTest, RejectsTooFewSamples)
{
  auto bytes = denseStream(zeroDistances(), materialsOf(1));
  ASSERT_NE(terrain::decodeChunk(bytes), nullptr);

  // Drop the final material run packet: the material array now ends short.
  bytes.resize(bytes.size() - 2);
  EXPECT_EQ(terrain::decodeChunk(bytes), nullptr);
}

TEST(TerrainCodecTest, RejectsADistanceOutsideTheBandAnywhereInTheChunk)
{
  const RawSamples valid = validSamples();
  ASSERT_NE(terrain::decodeChunk(literalStream(valid.distances, valid.materials)), nullptr);

  for (const size_t at : { samples / 2, samples - 1 })
  {
    for (const uint16_t bad : { uint16_t{ 32765 }, uint16_t{ 0x8003 }, uint16_t{ 0x8000 }, uint16_t{ 0x8001 } })
    {
      RawSamples raw = validSamples();
      raw.distances[at] = bad;

      EXPECT_EQ(terrain::decodeChunk(literalStream(raw.distances, raw.materials)), nullptr) << at << " " << bad;
    }

    for (const uint16_t edge : { uint16_t{ 32764 }, uint16_t{ 0x8004 } })
    {
      RawSamples raw = validSamples();
      raw.distances[at] = edge;

      EXPECT_NE(terrain::decodeChunk(literalStream(raw.distances, raw.materials)), nullptr) << at << " " << edge;
    }
  }

  const std::vector<uint8_t> uniformOutside{ 0, 0xFF, 0x7F, 1 };
  const std::vector<uint8_t> uniformInside{ 0, 0xFC, 0x7F, 1 };
  EXPECT_EQ(terrain::decodeChunk(uniformOutside), nullptr);
  EXPECT_NE(terrain::decodeChunk(uniformInside), nullptr);
}

TEST(TerrainCodecTest, RejectsTheReservedMaterialAnywhereInTheChunk)
{
  for (const size_t at : { samples / 2, samples - 1 })
  {
    RawSamples raw = validSamples();
    raw.materials[at] = 254;
    EXPECT_NE(terrain::decodeChunk(literalStream(raw.distances, raw.materials)), nullptr) << at;

    raw.materials[at] = terrain::reservedMaterial;
    EXPECT_EQ(terrain::decodeChunk(literalStream(raw.distances, raw.materials)), nullptr) << at;
  }

  const std::vector<uint8_t> uniformReserved{ 0, 0, 0, 255 };
  const std::vector<uint8_t> uniformOk{ 0, 0, 0, 254 };
  EXPECT_EQ(terrain::decodeChunk(uniformReserved), nullptr);
  EXPECT_NE(terrain::decodeChunk(uniformOk), nullptr);
}

TEST(TerrainCodecTest, NonCanonicalStreamsDecodeToTheSameChunkAsTheCanonicalOne)
{
  const RawSamples raw = validSamples();
  const Bytes literal = literalStream(raw.distances, raw.materials);

  std::vector<int16_t> distances;

  for (const uint16_t value : raw.distances)
  {
    distances.push_back(static_cast<int16_t>(value));
  }

  const auto canonical = terrain::TerrainChunk::dense(distances, raw.materials);
  ASSERT_NE(canonical, nullptr);

  EXPECT_NE(literal, terrain::encodeChunk(*canonical));

  const auto decoded = terrain::decodeChunk(literal);
  ASSERT_NE(decoded, nullptr);
  EXPECT_EQ(decoded->hash(), canonical->hash());

  // A dense tag whose content is uniform decodes to the uniform chunk.
  const std::vector<uint16_t> sevens(samples, 7);
  const std::vector<uint8_t> threes(samples, 3);
  const auto collapsed = terrain::decodeChunk(literalStream(sevens, threes));

  ASSERT_NE(collapsed, nullptr);
  EXPECT_TRUE(collapsed->isUniform());
  EXPECT_EQ(collapsed->hash(), terrain::TerrainChunk::uniform(7, 3)->hash());
}
