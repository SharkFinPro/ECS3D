#include "EditorCameraSettings.h"
#include "SettingsStore.h"
#include <algorithm>
#include <cmath>

namespace editorCameraSettings {
  float clampSpeed(const float speed)
  {
    if (!std::isfinite(speed))
    {
      return defaultSpeed;
    }

    return std::clamp(speed, minSpeed, maxSpeed);
  }

  float readSpeed(const SettingsStore& settings)
  {
    return clampSpeed(settings.get<float>(key, defaultSpeed));
  }

  void writeSpeed(SettingsStore& settings, const float speed)
  {
    settings.set(key, clampSpeed(speed));
  }
}
