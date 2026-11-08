#ifndef CLIENTDISPLAYSETTINGS_H
#define CLIENTDISPLAYSETTINGS_H

#include <filesystem>

class SettingsStore;

namespace clientDisplaySettings {
  inline constexpr bool defaultFullscreen = false;

  inline constexpr const char* fullscreenKey = "client.display.fullscreen";

  // Its own file, not settings.json: a SettingsStore rewrites the whole file from what it loaded at startup, so
  // a client running next to an editor would overwrite the editor's later changes.
  [[nodiscard]] std::filesystem::path defaultFile();

  [[nodiscard]] bool readFullscreen(const SettingsStore& settings);
  void writeFullscreen(SettingsStore& settings, bool fullscreen);

  // The --windowed flag overrides the remembered preference for this launch and never changes it.
  [[nodiscard]] bool startsFullscreen(const SettingsStore& settings, bool windowedFlag);
}

#endif  // CLIENTDISPLAYSETTINGS_H
