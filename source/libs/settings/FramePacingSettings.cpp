#include "FramePacingSettings.h"
#include "SettingsStore.h"
#include <algorithm>
#include <charconv>
#include <cmath>

namespace framePacingSettings {
  int clampMaxFps(const int framesPerSecond)
  {
    if (framesPerSecond <= 0)
    {
      return unlimited;
    }

    return std::clamp(framesPerSecond, minMaxFps, maxMaxFps);
  }

  bool readVsync(const SettingsStore& settings, const Keys& keys)
  {
    return settings.get<bool>(keys.vsync, defaultVsync);
  }

  void writeVsync(SettingsStore& settings, const Keys& keys, const bool vsync)
  {
    settings.set(keys.vsync, vsync);
  }

  int readMaxFps(const SettingsStore& settings, const Keys& keys)
  {
    // Read as a double so a hand-written huge or fractional number is clamped rather than converted out of range.
    const double value = settings.get<double>(keys.maxFps, static_cast<double>(defaultMaxFps));

    if (!std::isfinite(value))
    {
      return defaultMaxFps;
    }

    if (value >= static_cast<double>(maxMaxFps))
    {
      return maxMaxFps;
    }

    if (value <= 0.0)
    {
      return unlimited;
    }

    return clampMaxFps(static_cast<int>(value));
  }

  void writeMaxFps(SettingsStore& settings, const Keys& keys, const int framesPerSecond)
  {
    settings.set(keys.maxFps, clampMaxFps(framesPerSecond));
  }

  std::optional<int> parseMaxFps(const std::string_view text)
  {
    if (text.empty() || text.front() < '0' || text.front() > '9')
    {
      return std::nullopt;
    }

    int value = 0;
    const char* end = text.data() + text.size();
    const auto [parsedEnd, error] = std::from_chars(text.data(), end, value);

    if (error != std::errc() || parsedEnd != end)
    {
      return std::nullopt;
    }

    return clampMaxFps(value);
  }

  bool resolveVsync(const SettingsStore& settings, const Keys& keys, const std::optional<bool> flag)
  {
    return flag.value_or(readVsync(settings, keys));
  }

  int resolveMaxFps(const SettingsStore& settings, const Keys& keys, const std::optional<int> flag)
  {
    return flag ? clampMaxFps(*flag) : readMaxFps(settings, keys);
  }
}
