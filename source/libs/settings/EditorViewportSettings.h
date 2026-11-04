#ifndef EDITORVIEWPORTSETTINGS_H
#define EDITORVIEWPORTSETTINGS_H

class SettingsStore;

namespace editorViewportSettings {
  inline constexpr bool defaultShowGrid = true;

  inline constexpr bool defaultStatsReadout = false;

  inline constexpr const char* showGridKey = "editor.viewport.showGrid";
  inline constexpr const char* statsReadoutKey = "editor.viewport.statsReadout";

  [[nodiscard]] bool readShowGrid(const SettingsStore& settings);
  void writeShowGrid(SettingsStore& settings, bool show);

  [[nodiscard]] bool readStatsReadout(const SettingsStore& settings);
  void writeStatsReadout(SettingsStore& settings, bool show);
}

#endif  // EDITORVIEWPORTSETTINGS_H
