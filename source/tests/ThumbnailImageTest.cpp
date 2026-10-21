#include <gtest/gtest.h>

#include "ThumbnailImage.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>

using namespace thumbnail;

namespace
{
  using Color = std::array<std::uint8_t, 3>;

  constexpr Color barColor = {barRed, barGreen, barBlue};
  constexpr Color red = {200, 30, 40};
  constexpr Color blue = {20, 60, 220};

  std::vector<std::uint8_t> solidRgba(const std::uint32_t width, const std::uint32_t height, const Color& color)
  {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    for (std::size_t i = 0; i < pixels.size(); i += 4)
    {
      pixels[i] = color[0];
      pixels[i + 1] = color[1];
      pixels[i + 2] = color[2];
      pixels[i + 3] = 255;
    }
    return pixels;
  }

  Color pixelAt(const RgbImage& image, const std::size_t x, const std::size_t y)
  {
    const std::size_t at = (y * image.width + x) * 3;
    return {image.pixels[at], image.pixels[at + 1], image.pixels[at + 2]};
  }

  void expectColorNear(const Color& actual, const Color& expected, const int tolerance)
  {
    for (std::size_t c = 0; c < 3; ++c)
    {
      EXPECT_NEAR(actual[c], expected[c], tolerance) << "channel " << c;
    }
  }

  void expectOutputSize(const RgbImage& image)
  {
    EXPECT_EQ(image.width, thumbnailWidth);
    EXPECT_EQ(image.height, thumbnailHeight);
    EXPECT_EQ(image.pixels.size(), static_cast<std::size_t>(thumbnailWidth) * thumbnailHeight * 3);
  }
}

TEST(ThumbnailImageTest, SixteenByNineFillsTheTargetWithNoBars)
{
  const auto image = letterbox(solidRgba(1280, 720, red), 1280, 720, PixelFormat::rgba8);

  expectOutputSize(image);
  expectColorNear(pixelAt(image, 0, 0), red, 1);
  expectColorNear(pixelAt(image, thumbnailWidth - 1, thumbnailHeight - 1), red, 1);
}

TEST(ThumbnailImageTest, FourByThreeIsPillarboxed)
{
  const auto image = letterbox(solidRgba(800, 600, red), 800, 600, PixelFormat::rgba8);

  expectOutputSize(image);
  // 4:3 at 360 high is 480 wide, leaving 80 columns of bar on each side.
  EXPECT_EQ(pixelAt(image, 0, 180), barColor);
  EXPECT_EQ(pixelAt(image, 79, 180), barColor);
  EXPECT_EQ(pixelAt(image, 560, 180), barColor);
  EXPECT_EQ(pixelAt(image, 639, 180), barColor);
  expectColorNear(pixelAt(image, 80, 180), red, 1);
  expectColorNear(pixelAt(image, 559, 180), red, 1);
  expectColorNear(pixelAt(image, 0, 0), barColor, 0);
  expectColorNear(pixelAt(image, 80, 0), red, 1);
}

TEST(ThumbnailImageTest, TwentyOneByNineIsLetterboxed)
{
  const auto image = letterbox(solidRgba(2100, 900, red), 2100, 900, PixelFormat::rgba8);

  expectOutputSize(image);
  // 21:9 at 640 wide is 274 high, so 43 rows of bar above and below.
  EXPECT_EQ(pixelAt(image, 320, 0), barColor);
  EXPECT_EQ(pixelAt(image, 320, 42), barColor);
  EXPECT_EQ(pixelAt(image, 320, 317), barColor);
  EXPECT_EQ(pixelAt(image, 320, 359), barColor);
  expectColorNear(pixelAt(image, 320, 43), red, 1);
  expectColorNear(pixelAt(image, 320, 316), red, 1);
  expectColorNear(pixelAt(image, 0, 43), red, 1);
  expectColorNear(pixelAt(image, 639, 316), red, 1);
}

TEST(ThumbnailImageTest, TinyInputIsEnlargedAndCentered)
{
  const auto image = letterbox(solidRgba(2, 2, blue), 2, 2, PixelFormat::rgba8);

  expectOutputSize(image);
  // A square fits as 360 x 360, leaving 140 columns of bar on each side.
  EXPECT_EQ(pixelAt(image, 139, 180), barColor);
  EXPECT_EQ(pixelAt(image, 500, 180), barColor);
  expectColorNear(pixelAt(image, 140, 0), blue, 1);
  expectColorNear(pixelAt(image, 499, 359), blue, 1);
}

TEST(ThumbnailImageTest, SolidColorSurvivesANonIntegerRatio)
{
  const auto image = letterbox(solidRgba(1366, 768, red), 1366, 768, PixelFormat::rgba8);

  expectOutputSize(image);
  for (const std::size_t x : {0u, 1u, 319u, 638u, 639u})
  {
    for (const std::size_t y : {0u, 1u, 179u, 358u, 359u})
    {
      expectColorNear(pixelAt(image, x, y), red, 1);
    }
  }
}

TEST(ThumbnailImageTest, HalfAndHalfKeepsItsSplitColumn)
{
  constexpr std::uint32_t width = 800;
  constexpr std::uint32_t height = 600;
  auto pixels = solidRgba(width, height, red);
  for (std::size_t y = 0; y < height; ++y)
  {
    for (std::size_t x = width / 2; x < width; ++x)
    {
      const std::size_t at = (y * width + x) * 4;
      pixels[at] = blue[0];
      pixels[at + 1] = blue[1];
      pixels[at + 2] = blue[2];
    }
  }

  const auto image = letterbox(pixels, width, height, PixelFormat::rgba8);

  // The image spans columns 80..559, so the split lands between columns 319 and 320.
  expectColorNear(pixelAt(image, 318, 100), red, 1);
  expectColorNear(pixelAt(image, 319, 100), red, 1);
  expectColorNear(pixelAt(image, 320, 100), blue, 1);
  expectColorNear(pixelAt(image, 321, 100), blue, 1);
}

TEST(ThumbnailImageTest, BgraInputSwapsRedAndBlue)
{
  std::vector<std::uint8_t> pixels(4 * 4 * 4);
  for (std::size_t i = 0; i < pixels.size(); i += 4)
  {
    pixels[i] = 10;
    pixels[i + 1] = 20;
    pixels[i + 2] = 30;
    pixels[i + 3] = 255;
  }

  const auto asRgba = letterbox(pixels, 4, 4, PixelFormat::rgba8);
  const auto asBgra = letterbox(pixels, 4, 4, PixelFormat::bgra8);

  expectColorNear(pixelAt(asRgba, 320, 180), {10, 20, 30}, 0);
  expectColorNear(pixelAt(asBgra, 320, 180), {30, 20, 10}, 0);
}

TEST(ThumbnailImageTest, EmptyOrInconsistentInputGivesAnEmptyResult)
{
  const auto good = solidRgba(8, 8, red);

  EXPECT_FALSE(letterbox(good, 8, 8, PixelFormat::rgba8).empty());
  EXPECT_TRUE(letterbox({}, 8, 8, PixelFormat::rgba8).empty());
  EXPECT_TRUE(letterbox(good, 0, 8, PixelFormat::rgba8).empty());
  EXPECT_TRUE(letterbox(good, 8, 0, PixelFormat::rgba8).empty());
  EXPECT_TRUE(letterbox(good, 8, 9, PixelFormat::rgba8).empty());
  EXPECT_TRUE(letterbox(good, 8, 8, PixelFormat::rgba8, 0, 360).empty());
  EXPECT_TRUE(makeThumbnailPng(good, 8, 9, PixelFormat::rgba8).empty());
}

TEST(ThumbnailImageTest, EncodingAnInconsistentImageGivesNoBytes)
{
  RgbImage image;
  image.width = 4;
  image.height = 4;
  image.pixels.resize(10);

  EXPECT_TRUE(encodePng(image).empty());
  EXPECT_TRUE(encodePng(RgbImage{}).empty());
}

TEST(ThumbnailImageTest, PngHasTheSignatureAndTheTargetSize)
{
  const auto png = makeThumbnailPng(solidRgba(1280, 720, red), 1280, 720, PixelFormat::rgba8);

  ASSERT_GE(png.size(), 24u);
  const std::array<std::uint8_t, 8> signature = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  EXPECT_TRUE(std::equal(signature.begin(), signature.end(), png.begin()));

  const auto bigEndian = [&png](const std::size_t at)
  {
    return (static_cast<std::uint32_t>(png[at]) << 24) | (static_cast<std::uint32_t>(png[at + 1]) << 16) |
           (static_cast<std::uint32_t>(png[at + 2]) << 8) | static_cast<std::uint32_t>(png[at + 3]);
  };
  EXPECT_EQ(bigEndian(16), thumbnailWidth);
  EXPECT_EQ(bigEndian(20), thumbnailHeight);
}

TEST(ThumbnailImageTest, PngDecodesToExactlyTheLetterboxedPixels)
{
  constexpr std::uint32_t width = 800;
  constexpr std::uint32_t height = 600;
  auto pixels = solidRgba(width, height, red);
  for (std::size_t y = 0; y < height; ++y)
  {
    for (std::size_t x = width / 2; x < width; ++x)
    {
      const std::size_t at = (y * width + x) * 4;
      pixels[at] = blue[0];
      pixels[at + 1] = blue[1];
      pixels[at + 2] = blue[2];
    }
  }

  const auto expected = letterbox(pixels, width, height, PixelFormat::rgba8);
  const auto png = encodePng(expected);
  ASSERT_FALSE(png.empty());

  int decodedWidth = 0;
  int decodedHeight = 0;
  int components = 0;
  stbi_uc* decoded =
    stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &decodedWidth, &decodedHeight, &components, 3);
  ASSERT_NE(decoded, nullptr);

  EXPECT_EQ(decodedWidth, static_cast<int>(thumbnailWidth));
  EXPECT_EQ(decodedHeight, static_cast<int>(thumbnailHeight));
  EXPECT_TRUE(std::equal(expected.pixels.begin(), expected.pixels.end(), decoded));
  stbi_image_free(decoded);
}
