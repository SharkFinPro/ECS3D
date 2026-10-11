#ifndef EDITORVIEWPORTSETTINGS_H
#define EDITORVIEWPORTSETTINGS_H

class SettingsStore;

namespace editorViewportSettings {
  inline constexpr bool defaultShowGrid = true;

  inline constexpr const char* showGridKey = "editor.viewport.showGrid";

  [[nodiscard]] bool readShowGrid(const SettingsStore& settings);
  void writeShowGrid(SettingsStore& settings, bool show);
}

#endif  // EDITORVIEWPORTSETTINGS_H
