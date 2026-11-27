#include "EditorTerrainSettings.h"
#include "SettingsStore.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>

namespace editorTerrainSettings {
  namespace {
    constexpr std::array<const char*, toolCount> toolIds = { "add", "subtract", "smooth", "flatten" };

    std::string brushKey(const std::size_t tool, const char* field)
    {
      return std::string("editor.terrain.") + toolIds[tool] + "." + field;
    }

    float clampOrDefault(const float value, const float low, const float high, const float fallback)
    {
      return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
    }
  }

  const char* toolId(const std::size_t tool)
  {
    return tool < toolCount ? toolIds[tool] : nullptr;
  }

  float clampRadius(const float radius)
  {
    return clampOrDefault(radius, minRadius, maxRadius, defaultRadius);
  }

  float clampStrength(const float strength)
  {
    return clampOrDefault(strength, minStrength, maxStrength, defaultStrength);
  }

  float defaultStrengthFor(const std::size_t tool)
  {
    return tool < toolCount && std::string_view(toolIds[tool]) == "subtract" ? defaultSubtractStrength : defaultStrength;
  }

  float clampFalloff(const float falloff)
  {
    return clampOrDefault(falloff, minFalloff, maxFalloff, defaultFalloff);
  }

  BrushSettings readBrush(const SettingsStore& settings, const std::size_t tool)
  {
    if (tool >= toolCount)
    {
      return {};
    }

    return {
      clampRadius(settings.get<float>(brushKey(tool, "radius"), defaultRadius)),
      clampOrDefault(settings.get<float>(brushKey(tool, "strength"), defaultStrengthFor(tool)), minStrength, maxStrength,
                     defaultStrengthFor(tool)),
      clampFalloff(settings.get<float>(brushKey(tool, "falloff"), defaultFalloff))
    };
  }

  void writeBrush(SettingsStore& settings, const std::size_t tool, const BrushSettings& brush)
  {
    if (tool >= toolCount)
    {
      return;
    }

    settings.set(brushKey(tool, "radius"), clampRadius(brush.radius));
    settings.set(brushKey(tool, "strength"),
                 clampOrDefault(brush.strength, minStrength, maxStrength, defaultStrengthFor(tool)));
    settings.set(brushKey(tool, "falloff"), clampFalloff(brush.falloff));
  }

  std::size_t readLastTool(const SettingsStore& settings)
  {
    const std::string id = settings.get<std::string>(lastToolKey, "");

    for (std::size_t tool = 0; tool < toolCount; ++tool)
    {
      if (id == toolIds[tool])
      {
        return tool;
      }
    }

    return 0;
  }

  void writeLastTool(SettingsStore& settings, const std::size_t tool)
  {
    if (tool >= toolCount)
    {
      return;
    }

    settings.set(lastToolKey, std::string(toolIds[tool]));
  }
}
