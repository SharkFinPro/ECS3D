#ifndef THUMBNAILIMAGE_H
#define THUMBNAILIMAGE_H

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace thumbnail
{
  constexpr std::uint32_t thumbnailWidth = 640;
  constexpr std::uint32_t thumbnailHeight = 360;

  // Matches the launcher's background (BgBrush in Theme.axaml), so the bars blend into a project card.
  constexpr std::uint8_t barRed = 0x14;
  constexpr std::uint8_t barGreen = 0x15;
  constexpr std::uint8_t barBlue = 0x18;

  enum class PixelFormat
  {
    rgba8,
    bgra8
  };

  // Tightly packed rows, top row first, three bytes per pixel. RGB rather than RGBA: a thumbnail is
  // always opaque, and dropping alpha keeps the PNG smaller.
  struct RgbImage
  {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> pixels;

    [[nodiscard]] bool empty() const
    {
      return pixels.empty();
    }
  };

  // Fits the source inside the target keeping its aspect ratio, centers it, and fills the bars with the
  // bar color. A larger source is shrunk by area averaging (box filter with fractional edge coverage),
  // a smaller one enlarged by bilinear interpolation. Source alpha is ignored. Returns an empty image
  // when a size is zero, a source side exceeds 65535, or the buffer holds fewer than width * height * 4 bytes.
  [[nodiscard]] RgbImage letterbox(std::span<const std::uint8_t> source, std::uint32_t width, std::uint32_t height,
                                   PixelFormat format, std::uint32_t targetWidth = thumbnailWidth,
                                   std::uint32_t targetHeight = thumbnailHeight);

  // PNG bytes for the image, or empty when it is empty or its buffer does not match its size.
  [[nodiscard]] std::vector<std::uint8_t> encodePng(const RgbImage& image);

  [[nodiscard]] std::vector<std::uint8_t> makeThumbnailPng(std::span<const std::uint8_t> source,
                                                           std::uint32_t width, std::uint32_t height,
                                                           PixelFormat format);
}

#endif
