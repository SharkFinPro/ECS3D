#include "EditorViewportSettings.h"
#include "SettingsStore.h"

namespace editorViewportSettings {
  bool readShowGrid(const SettingsStore& settings)
  {
    return settings.get<bool>(showGridKey, defaultShowGrid);
  }

  void writeShowGrid(SettingsStore& settings, const bool show)
  {
    settings.set(showGridKey, show);
  }

  bool readStatsReadout(const SettingsStore& settings)
  {
    return settings.get<bool>(statsReadoutKey, defaultStatsReadout);
  }

  void writeStatsReadout(SettingsStore& settings, const bool show)
  {
    settings.set(statsReadoutKey, show);
  }

  bool readRayTracing(const SettingsStore& settings)
  {
    return settings.get<bool>(rayTracingKey, defaultRayTracing);
  }

  void writeRayTracing(SettingsStore& settings, const bool enabled)
  {
    settings.set(rayTracingKey, enabled);
  }

  bool rayTracingAtStartup(const bool stored, const bool supported)
  {
    return stored && supported;
  }
}
