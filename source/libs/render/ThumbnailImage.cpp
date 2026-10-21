#include "ThumbnailImage.h"
#include <algorithm>
#include <cmath>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>

namespace thumbnail
{
  namespace
  {
    struct Tap
    {
      std::size_t index;
      float weight;
    };

    // For each destination coordinate, the source coordinates that feed it, with weights summing to 1.
    std::vector<std::vector<Tap>> axisTaps(const std::uint32_t sourceSize, const std::uint32_t destSize)
    {
      std::vector<std::vector<Tap>> taps(destSize);
      const double ratio = static_cast<double>(sourceSize) / static_cast<double>(destSize);

      for (std::uint32_t d = 0; d < destSize; ++d)
      {
        if (destSize < sourceSize)
        {
          const double lo = d * ratio;
          const double hi = (d + 1) * ratio;
          const auto first = static_cast<std::size_t>(std::floor(lo));
          const auto last = std::min<std::size_t>(static_cast<std::size_t>(std::ceil(hi)), sourceSize);

          for (std::size_t i = first; i < last; ++i)
          {
            const double coverage = std::min(hi, static_cast<double>(i + 1)) - std::max(lo, static_cast<double>(i));
            if (coverage > 0.0)
            {
              taps[d].push_back({i, static_cast<float>(coverage / ratio)});
            }
          }
        }
        else
        {
          const double center = (d + 0.5) * ratio - 0.5;
          const double base = std::floor(center);
          const auto fraction = static_cast<float>(center - base);
          const auto maxIndex = static_cast<std::ptrdiff_t>(sourceSize) - 1;
          const auto i0 = std::clamp<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(base), 0, maxIndex);
          const auto i1 = std::clamp<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(base) + 1, 0, maxIndex);

          taps[d].push_back({static_cast<std::size_t>(i0), 1.0f - fraction});
          taps[d].push_back({static_cast<std::size_t>(i1), fraction});
        }
      }

      return taps;
    }

    std::uint32_t scaledSize(const std::uint64_t value, const std::uint64_t numerator,
                             const std::uint64_t denominator)
    {
      return static_cast<std::uint32_t>(
        std::max<std::uint64_t>(1, (value * numerator + denominator / 2) / denominator));
    }

    void appendBytes(void* context, void* data, const int size)
    {
      auto* out = static_cast<std::vector<std::uint8_t>*>(context);
      const auto* bytes = static_cast<const std::uint8_t*>(data);
      out->insert(out->end(), bytes, bytes + size);
    }
  }

  RgbImage letterbox(const std::span<const std::uint8_t> source, const std::uint32_t width,
                     const std::uint32_t height, const PixelFormat format, const std::uint32_t targetWidth,
                     const std::uint32_t targetHeight)
  {
    if (width == 0 || height == 0 || targetWidth == 0 || targetHeight == 0)
    {
      return {};
    }

    const std::uint64_t required = static_cast<std::uint64_t>(width) * height * 4;
    if (source.size() < required)
    {
      return {};
    }

    std::uint32_t scaledWidth = targetWidth;
    std::uint32_t scaledHeight = targetHeight;
    if (static_cast<std::uint64_t>(width) * targetHeight >= static_cast<std::uint64_t>(height) * targetWidth)
    {
      scaledHeight = std::min(targetHeight, scaledSize(height, targetWidth, width));
    }
    else
    {
      scaledWidth = std::min(targetWidth, scaledSize(width, targetHeight, height));
    }

    const auto columnTaps = axisTaps(width, scaledWidth);
    const auto rowTaps = axisTaps(height, scaledHeight);
    const std::size_t redIndex = format == PixelFormat::bgra8 ? 2 : 0;
    const std::size_t blueIndex = 2 - redIndex;

    std::vector<float> horizontal(static_cast<std::size_t>(scaledWidth) * height * 3);
    for (std::size_t y = 0; y < height; ++y)
    {
      const std::uint8_t* row = source.data() + y * width * 4;
      for (std::size_t x = 0; x < scaledWidth; ++x)
      {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        for (const Tap& tap : columnTaps[x])
        {
          const std::uint8_t* pixel = row + tap.index * 4;
          r += tap.weight * pixel[redIndex];
          g += tap.weight * pixel[1];
          b += tap.weight * pixel[blueIndex];
        }

        float* out = &horizontal[(y * scaledWidth + x) * 3];
        out[0] = r;
        out[1] = g;
        out[2] = b;
      }
    }

    RgbImage result;
    result.width = targetWidth;
    result.height = targetHeight;
    result.pixels.resize(static_cast<std::size_t>(targetWidth) * targetHeight * 3);
    for (std::size_t i = 0; i < result.pixels.size(); i += 3)
    {
      result.pixels[i] = barRed;
      result.pixels[i + 1] = barGreen;
      result.pixels[i + 2] = barBlue;
    }

    const std::size_t offsetX = (targetWidth - scaledWidth) / 2;
    const std::size_t offsetY = (targetHeight - scaledHeight) / 2;
    for (std::size_t y = 0; y < scaledHeight; ++y)
    {
      for (std::size_t x = 0; x < scaledWidth; ++x)
      {
        float sum[3] = {0.0f, 0.0f, 0.0f};
        for (const Tap& tap : rowTaps[y])
        {
          const float* in = &horizontal[(tap.index * scaledWidth + x) * 3];
          sum[0] += tap.weight * in[0];
          sum[1] += tap.weight * in[1];
          sum[2] += tap.weight * in[2];
        }

        std::uint8_t* out = &result.pixels[((y + offsetY) * targetWidth + x + offsetX) * 3];
        for (int c = 0; c < 3; ++c)
        {
          out[c] = static_cast<std::uint8_t>(std::clamp(std::lround(sum[c]), 0L, 255L));
        }
      }
    }

    return result;
  }

  std::vector<std::uint8_t> encodePng(const RgbImage& image)
  {
    const std::uint64_t required = static_cast<std::uint64_t>(image.width) * image.height * 3;
    if (image.width == 0 || image.height == 0 || image.pixels.size() != required)
    {
      return {};
    }

    std::vector<std::uint8_t> bytes;
    const int ok = stbi_write_png_to_func(appendBytes, &bytes, static_cast<int>(image.width),
                                          static_cast<int>(image.height), 3, image.pixels.data(),
                                          static_cast<int>(image.width * 3));
    if (ok == 0)
    {
      return {};
    }

    return bytes;
  }

  std::vector<std::uint8_t> makeThumbnailPng(const std::span<const std::uint8_t> source,
                                             const std::uint32_t width, const std::uint32_t height,
                                             const PixelFormat format)
  {
    return encodePng(letterbox(source, width, height, format));
  }
}
