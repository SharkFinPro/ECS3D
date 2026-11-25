#ifndef EDITORVIEWPORTSETTINGS_H
#define EDITORVIEWPORTSETTINGS_H

class SettingsStore;

namespace editorViewportSettings {
  inline constexpr bool defaultShowGrid = true;

  inline constexpr bool defaultStatsReadout = false;

  // Matches the engine's own default, so a user who never touched the setting sees no change.
  inline constexpr bool defaultRayTracing = true;

  inline constexpr const char* showGridKey = "editor.viewport.showGrid";
  inline constexpr const char* statsReadoutKey = "editor.viewport.statsReadout";
  inline constexpr const char* rayTracingKey = "editor.display.rayTracing";

  [[nodiscard]] bool readShowGrid(const SettingsStore& settings);
  void writeShowGrid(SettingsStore& settings, bool show);

  [[nodiscard]] bool readStatsReadout(const SettingsStore& settings);
  void writeStatsReadout(SettingsStore& settings, bool show);

  [[nodiscard]] bool readRayTracing(const SettingsStore& settings);
  void writeRayTracing(SettingsStore& settings, bool enabled);

  // A stored preference never enables ray tracing on a device that cannot do it.
  [[nodiscard]] bool rayTracingAtStartup(bool stored, bool supported);
}

#endif  // EDITORVIEWPORTSETTINGS_H
