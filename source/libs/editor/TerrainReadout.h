#ifndef TERRAINREADOUT_H
#define TERRAINREADOUT_H

#include <TerrainSurfacePlan.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

// The Terrain inspector's read-only progress rows.
namespace terrain
{
  // nullopt held means the component has no volume.
  [[nodiscard]] inline std::string terrainChunksText(const std::optional<size_t> held, const uint32_t expected)
  {
    if (!held)
    {
      return "-";
    }

    if (expected == 0 || *held > expected)
    {
      return std::to_string(*held);
    }

    return std::to_string(*held) + " / " + std::to_string(expected);
  }

  // "ready" is the only state that shows no percentage, so a nonzero fraction never rounds to 0% or 100%.
  [[nodiscard]] inline std::string terrainSurfaceText(const TerrainSurfaceStatus& status)
  {
    if (!status.hasSurface)
    {
      return status.tooLarge ? "too large to draw" : "none";
    }

    const std::string suffix = status.tooLarge ? ", too large to grow" : "";
    const float pending = std::isfinite(status.pending) ? std::clamp(status.pending, 0.0f, 1.0f) : 1.0f;
    if (pending <= 0.0f)
    {
      return "ready" + suffix;
    }

    int percent = std::max(static_cast<int>(std::lround(pending * 100.0f)), 1);
    if (pending < 1.0f)
    {
      percent = std::min(percent, 99);
    }

    const std::string pendingText = std::to_string(percent) + "% pending";
    return (status.growing ? "growing, " + pendingText : pendingText) + suffix;
  }
}

#endif //TERRAINREADOUT_H
