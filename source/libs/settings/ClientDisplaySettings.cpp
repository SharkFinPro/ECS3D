#include "ClientDisplaySettings.h"
#include "SettingsStore.h"
#include <UserDataDirectory.h>

namespace clientDisplaySettings {
  std::filesystem::path defaultFile()
  {
    return userDataDirectory() / "client-settings.json";
  }

  bool readFullscreen(const SettingsStore& settings)
  {
    return settings.get<bool>(fullscreenKey, defaultFullscreen);
  }

  void writeFullscreen(SettingsStore& settings, const bool fullscreen)
  {
    settings.set(fullscreenKey, fullscreen);
  }

  bool startsFullscreen(const SettingsStore& settings, const bool windowedFlag)
  {
    return !windowedFlag && readFullscreen(settings);
  }
}
