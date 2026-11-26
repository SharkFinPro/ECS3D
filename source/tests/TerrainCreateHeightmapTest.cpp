#include <gtest/gtest.h>

#include "TerrainCreateFlow.h"

#include "TerrainCreateTestHelpers.h"
#include "ProjectPaths.h"
#include "assets/AssetRegistry.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

using namespace terrainCreateTest;

namespace {
  HeightmapDecode decodeUtf8(const std::string& utf8Path, const int64_t maxSamples)
  {
    return terrainCreate::decodeHeightmap(pathFromUtf8(utf8Path.c_str()), maxSamples);
  }

  // Scratch files live in a per-test folder that is removed with the fixture.
  class TerrainCreateHeightmap : public ::testing::Test
  {
  protected:
    // The folder name carries a non-ASCII character, as a user's folder can: the form hands the path over as UTF-8.
    void SetUp() override
    {
      const auto* info = testing::UnitTest::GetInstance()->current_test_info();
      const std::string name = std::string("ecs3d_terrain_create_cafÃ©_") + info->name() + "_" +
                               std::to_string(processId());
      m_dir = std::filesystem::temp_directory_path() / pathFromUtf8(name.c_str());
      std::filesystem::remove_all(m_dir);
      std::filesystem::create_directories(m_dir);
    }

    void TearDown() override
    {
      std::error_code ec;
      std::filesystem::remove_all(m_dir, ec);
    }

    // UTF-8, the way the file picker reports a path.
    [[nodiscard]] std::string path(const std::string& name) const
    {
      return pathToUtf8(m_dir / name);
    }

    [[nodiscard]] std::vector<uint8_t> readBytes(const std::string& utf8Path) const
    {
      std::ifstream in(pathFromUtf8(utf8Path.c_str()), std::ios::binary);

      return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
    }

    std::string writeBytes(const std::string& name, const std::vector<uint8_t>& bytes) const
    {
      std::ofstream out(m_dir / name, std::ios::binary);
      out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

      return path(name);
    }

    // An 8-bit PNG with the given channel count (1 = gray, 3 = RGB).
    std::string writePng8(const std::string& name, const int width, const int height, const int channels,
                          const std::vector<uint8_t>& samples) const
    {
      std::vector<uint8_t> out;
      const auto append = [](void* context, void* data, const int size) {
        auto* bytes = static_cast<std::vector<uint8_t>*>(context);
        const auto* begin = static_cast<const uint8_t*>(data);
        bytes->insert(bytes->end(), begin, begin + size);
      };
      EXPECT_NE(stbi_write_png_to_func(append, &out, width, height, channels, samples.data(), width * channels), 0);

      return writeBytes(name, out);
    }

    // A 16-bit grayscale PNG built by hand (stb writes only 8-bit): stored deflate blocks over big-endian samples.
    std::string writePng16(const std::string& name, const int width, const int height,
                           const std::vector<uint16_t>& samples) const
    {
      std::vector<uint8_t> raw;
      for (int row = 0; row < height; ++row)
      {
        raw.push_back(0);
        for (int column = 0; column < width; ++column)
        {
          const uint16_t value = samples[static_cast<size_t>(row) * width + column];
          raw.push_back(static_cast<uint8_t>(value >> 8));
          raw.push_back(static_cast<uint8_t>(value & 0xff));
        }
      }

      std::vector<uint8_t> zlib = { 0x78, 0x01 };
      for (size_t offset = 0; offset < raw.size() || offset == 0;)
      {
        const size_t length = std::min<size_t>(65535, raw.size() - offset);
        const bool last = offset + length >= raw.size();
        zlib.push_back(last ? 1 : 0);
        zlib.push_back(static_cast<uint8_t>(length & 0xff));
        zlib.push_back(static_cast<uint8_t>(length >> 8));
        zlib.push_back(static_cast<uint8_t>(~length & 0xff));
        zlib.push_back(static_cast<uint8_t>((~length >> 8) & 0xff));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                    raw.begin() + static_cast<std::ptrdiff_t>(offset + length));
        offset += length;

        if (last)
        {
          break;
        }
      }

      uint32_t a = 1;
      uint32_t b = 0;
      for (const uint8_t byte : raw)
      {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
      }
      appendBigEndian(zlib, (b << 16) | a);

      std::vector<uint8_t> png = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
      std::vector<uint8_t> header;
      appendBigEndian(header, static_cast<uint32_t>(width));
      appendBigEndian(header, static_cast<uint32_t>(height));
      header.insert(header.end(), { 16, 0, 0, 0, 0 });
      appendChunk(png, "IHDR", header);
      appendChunk(png, "IDAT", zlib);
      appendChunk(png, "IEND", {});

      return writeBytes(name, png);
    }

  private:
    static unsigned long processId()
    {
#ifdef _WIN32
      return static_cast<unsigned long>(_getpid());
#else
      return static_cast<unsigned long>(getpid());
#endif
    }

    static void appendBigEndian(std::vector<uint8_t>& bytes, const uint32_t value)
    {
      for (int shift = 24; shift >= 0; shift -= 8)
      {
        bytes.push_back(static_cast<uint8_t>((value >> shift) & 0xff));
      }
    }

    static uint32_t crc32(const std::vector<uint8_t>& bytes)
    {
      uint32_t crc = 0xffffffffu;
      for (const uint8_t byte : bytes)
      {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit)
        {
          crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
      }

      return ~crc;
    }

    static void appendChunk(std::vector<uint8_t>& png, const std::string& type, const std::vector<uint8_t>& data)
    {
      appendBigEndian(png, static_cast<uint32_t>(data.size()));
      std::vector<uint8_t> body(type.begin(), type.end());
      body.insert(body.end(), data.begin(), data.end());
      png.insert(png.end(), body.begin(), body.end());
      appendBigEndian(png, crc32(body));
    }

    std::filesystem::path m_dir;
  };
}

TEST_F(TerrainCreateHeightmap, AnEightBitImageDecodesScaledToSixteenBits)
{
  const auto file = writePng8("eight.png", 4, 2, 1, { 0, 1, 128, 255, 10, 20, 30, 40 });

  const auto decoded = decodeUtf8(file, 1000);
  ASSERT_TRUE(decoded.image.has_value()) << decoded.error;
  EXPECT_EQ(decoded.image->width, 4);
  EXPECT_EQ(decoded.image->height, 2);

  const std::vector<uint16_t> expected = { 0, 257, 128 * 257, 65535, 10 * 257, 20 * 257, 30 * 257, 40 * 257 };
  EXPECT_EQ(decoded.image->pixels, expected);
}

TEST_F(TerrainCreateHeightmap, ASixteenBitImageKeepsItsValues)
{
  const std::vector<uint16_t> samples = { 0, 1, 255, 256, 40000, 65534, 65535, 12345, 777 };
  const auto file = writePng16("sixteen.png", 3, 3, samples);

  const auto decoded = decodeUtf8(file, 1000);
  ASSERT_TRUE(decoded.image.has_value()) << decoded.error;
  EXPECT_EQ(decoded.image->width, 3);
  EXPECT_EQ(decoded.image->height, 3);
  EXPECT_EQ(decoded.image->pixels, samples);
}

TEST_F(TerrainCreateHeightmap, AColorImageIsConvertedToGray)
{
  const auto file = writePng8("color.png", 2, 1, 3, { 255, 255, 255, 0, 0, 0 });

  const auto decoded = decodeUtf8(file, 1000);
  ASSERT_TRUE(decoded.image.has_value()) << decoded.error;
  ASSERT_EQ(decoded.image->pixels.size(), 2u);
  EXPECT_EQ(decoded.image->pixels[0], 65535);
  EXPECT_EQ(decoded.image->pixels[1], 0);
}

TEST_F(TerrainCreateHeightmap, AnImageOverTheSampleLimitIsRefusedBeforeItIsDecoded)
{
  const auto file = writePng8("big.png", 64, 64, 1, std::vector<uint8_t>(64 * 64, 7));

  const auto refused = decodeUtf8(file, 64 * 64 - 1);
  EXPECT_FALSE(refused.image.has_value());
  EXPECT_NE(refused.error.find("64 x 64"), std::string::npos);

  const auto accepted = decodeUtf8(file, 64 * 64);
  ASSERT_TRUE(accepted.image.has_value()) << accepted.error;
  EXPECT_EQ(accepted.image->pixels.size(), 64u * 64u);
}

TEST_F(TerrainCreateHeightmap, AMissingEmptyOrNonImageFileIsRefused)
{
  EXPECT_FALSE(decodeUtf8(path("missing.png"), 1000).image.has_value());
  EXPECT_FALSE(decodeUtf8(writeBytes("empty.png", {}), 1000).image.has_value());

  const std::string text = "this is not an image";
  const auto notImage = writeBytes("text.png", std::vector<uint8_t>(text.begin(), text.end()));
  const auto refused = decodeUtf8(notImage, 1000);
  EXPECT_FALSE(refused.image.has_value());
  EXPECT_FALSE(refused.error.empty());

  EXPECT_TRUE(decodeUtf8(writePng8("ok.png", 2, 2, 1, { 1, 2, 3, 4 }), 1000).image.has_value());
}

TEST_F(TerrainCreateHeightmap, AHeightmapTerrainFollowsTheImageAndItsHeights)
{
  // Black on the left half of a 64 x 64 image, white on the right.
  std::vector<uint8_t> samples(64 * 64);
  for (int row = 0; row < 64; ++row)
  {
    for (int column = 0; column < 64; ++column)
    {
      samples[static_cast<size_t>(row) * 64 + column] = column < 32 ? 0 : 255;
    }
  }

  auto request = validFlat();
  request.fromHeightmap = true;
  request.heightmapPath = writePng8("steps.png", 64, 64, 1, samples);

  const auto result = terrainCreate::generateTerrain(request, registryWithExistingTerrain(), newTerrainUUID);
  ASSERT_TRUE(result.terrain.has_value()) << result.error;
  const auto& volume = *result.terrain->volume;

  EXPECT_EQ(result.terrain->description.chunkCount, volume.chunks().size());
  EXPECT_GT(volume.chunks().size(), 0u);

  // White is the full height scale: 32 m at 0.5 m voxels is 64 voxels.
  EXPECT_LT(volume.distanceAtSample({ 50, 60, 10 }), 0);
  EXPECT_GT(volume.distanceAtSample({ 50, 70, 10 }), 0);

  // Black is at the floor, with the solid layer under it.
  EXPECT_LT(volume.distanceAtSample({ 5, -1, 10 }), 0);
  EXPECT_GT(volume.distanceAtSample({ 5, 3, 10 }), 0);

  // The footprint is the image: nothing beyond its 64 columns.
  EXPECT_EQ(volume.distanceAtSample({ 70, -1, 10 }), terrain::airDistance);
}

TEST_F(TerrainCreateHeightmap, AHeightScaleThatNeedsTooManyChunksIsRefusedAndASmallOneIsNot)
{
  auto request = validFlat();
  request.fromHeightmap = true;
  request.heightmapPath = writePng8("small.png", 8, 8, 1, std::vector<uint8_t>(64, 200));
  const auto registry = registryWithExistingTerrain();

  EXPECT_TRUE(terrainCreate::generateTerrain(request, registry, newTerrainUUID).terrain.has_value());

  request.heightScaleMeters = 1.0e6f;
  const auto tall = terrainCreate::generateTerrain(request, registry, newTerrainUUID);
  EXPECT_FALSE(tall.terrain.has_value());
  EXPECT_NE(tall.error.find("chunks"), std::string::npos);
}

TEST_F(TerrainCreateHeightmap, AnImageAtTheChunkBoundIsCreatedAndOneColumnWiderIsRefused)
{
  auto request = validFlat();
  request.fromHeightmap = true;
  const auto registry = registryWithExistingTerrain();

  // 1024 x 1024 at 32 m over 0.5 m voxels is four chunks tall: exactly 4096 chunks.
  request.heightmapPath = writePng8("bound.png", 1024, 1024, 1, std::vector<uint8_t>(1024 * 1024, 200));
  const auto atBound = terrainCreate::generateTerrain(request, registry, newTerrainUUID);
  ASSERT_TRUE(atBound.terrain.has_value()) << atBound.error;
  EXPECT_LE(atBound.terrain->description.chunkCount, terrainCreate::maxCreateChunks);

  request.heightmapPath = writePng8("past.png", 1025, 1024, 1, std::vector<uint8_t>(1025 * 1024, 200));
  const auto past = terrainCreate::generateTerrain(request, registry, newTerrainUUID);
  EXPECT_FALSE(past.terrain.has_value());
  EXPECT_NE(past.error.find("4096"), std::string::npos);
}

TEST_F(TerrainCreateHeightmap, AHeaderNamingTooManySamplesIsRefusedWithoutDecodingTheBody)
{
  auto bytes = readBytes(writePng8("small2.png", 4, 4, 1, std::vector<uint8_t>(16, 9)));
  ASSERT_GT(bytes.size(), 24u);

  // IHDR width and height sit at offsets 16 and 20; stb does not check the chunk's CRC.
  const auto setDimension = [&bytes](const size_t offset, const uint32_t value) {
    bytes[offset] = static_cast<uint8_t>(value >> 24);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 16);
    bytes[offset + 2] = static_cast<uint8_t>(value >> 8);
    bytes[offset + 3] = static_cast<uint8_t>(value);
  };

  setDimension(16, 20000);
  setDimension(20, 20000);
  const auto huge = decodeUtf8(writeBytes("huge.png", bytes), terrainCreate::maxImageSamples);
  EXPECT_FALSE(huge.image.has_value());
  EXPECT_NE(huge.error.find("too large"), std::string::npos);

  // The control: a header inside the limit gets past that check, and then fails on its short body.
  setDimension(16, 1024);
  setDimension(20, 1024);
  const auto claimsLarge = decodeUtf8(writeBytes("claims.png", bytes),
                                                         terrainCreate::maxImageSamples);
  EXPECT_FALSE(claimsLarge.image.has_value());
  EXPECT_EQ(claimsLarge.error.find("too large"), std::string::npos);
  EXPECT_FALSE(claimsLarge.error.empty());
}

TEST_F(TerrainCreateHeightmap, ATruncatedImageWithAValidHeaderIsRefused)
{
  std::vector<uint8_t> noisy(128 * 128);
  uint32_t seed = 12345;
  for (auto& value : noisy)
  {
    seed = seed * 1664525u + 1013904223u;
    value = static_cast<uint8_t>(seed >> 24);
  }

  const auto full = readBytes(writePng8("whole.png", 128, 128, 1, noisy));
  ASSERT_GT(full.size(), 2000u);

  EXPECT_TRUE(decodeUtf8(path("whole.png"), terrainCreate::maxImageSamples).image.has_value());

  const std::vector<uint8_t> cut(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(full.size() / 2));
  const auto refused = decodeUtf8(writeBytes("cut.png", cut), terrainCreate::maxImageSamples);
  EXPECT_FALSE(refused.image.has_value());
  EXPECT_FALSE(refused.error.empty());
}

TEST_F(TerrainCreateHeightmap, ANonAsciiFolderNameIsReadAsUtf8)
{
  const auto file = writePng8("utf8.png", 2, 2, 1, { 1, 2, 3, 4 });
  ASSERT_NE(file.find("cafÃ©"), std::string::npos);

  auto request = validFlat();
  request.fromHeightmap = true;
  request.heightmapPath = file;

  const auto result = terrainCreate::generateTerrain(request, registryWithExistingTerrain(), newTerrainUUID);
  EXPECT_TRUE(result.terrain.has_value()) << result.error;

  request.heightmapPath = path("missing.png");
  EXPECT_FALSE(terrainCreate::generateTerrain(request, registryWithExistingTerrain(), newTerrainUUID).terrain
                 .has_value());
}
