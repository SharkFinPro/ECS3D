#ifndef EDITORTERRAINSETTINGS_H
#define EDITORTERRAINSETTINGS_H

#include <cstddef>

class SettingsStore;

// Terrain brush preferences, kept per tool. A tool is a plain index (add, subtract, smooth, flatten, paint, in the
// editor's TerrainTool order) because settings must not depend on the editor lib or data. Radius is in terrain-local
// world units, which the engine authors at 5 units per meter.
namespace editorTerrainSettings {
  inline constexpr std::size_t toolCount = 5;

  inline constexpr float defaultRadius = 10.0f;
  inline constexpr float minRadius = 0.1f;
  // The brush itself refuses a radius above maxBrushRadiusVoxels * voxelSize, so the user of these settings clamps
  // to the volume at use time.
  inline constexpr float maxRadius = 1000.0f;

  inline constexpr float defaultStrength = 0.5f;
  // Subtract starts gentler: it aims at the live terrain, so each application already reaches what the last exposed.
  inline constexpr float defaultSubtractStrength = 0.1f;
  inline constexpr float minStrength = 0.0f;
  inline constexpr float maxStrength = 1.0f;

  inline constexpr float defaultFalloff = 0.5f;
  inline constexpr float minFalloff = 0.0f;
  inline constexpr float maxFalloff = 1.0f;

  inline constexpr const char* lastToolKey = "editor.terrain.tool";

  struct BrushSettings {
    float radius = defaultRadius;
    float strength = defaultStrength;
    float falloff = defaultFalloff;
  };

  // The tool's id ("add", "subtract", "smooth", "flatten", "paint"), the settings-key fragment, or nullptr for an
  // out-of-range index.
  [[nodiscard]] const char* toolId(std::size_t tool);

  // The strength a tool starts at: defaultSubtractStrength for subtract, defaultStrength for the rest.
  [[nodiscard]] float defaultStrengthFor(std::size_t tool);

  // A non-finite input (a hand-edited settings file) returns the matching default.
  [[nodiscard]] float clampRadius(float radius);
  [[nodiscard]] float clampStrength(float strength);
  [[nodiscard]] float clampFalloff(float falloff);

  // Keys editor.terrain.<toolId>.radius|strength|falloff. An out-of-range tool reads the defaults and writes nothing.
  [[nodiscard]] BrushSettings readBrush(const SettingsStore& settings, std::size_t tool);
  void writeBrush(SettingsStore& settings, std::size_t tool, const BrushSettings& brush);

  // Stored as the tool's id. An unknown id or a stored value of the wrong type reads 0; an out-of-range index
  // writes nothing.
  [[nodiscard]] std::size_t readLastTool(const SettingsStore& settings);
  void writeLastTool(SettingsStore& settings, std::size_t tool);
}

#endif  // EDITORTERRAINSETTINGS_H
