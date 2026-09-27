#include "EditorGizmoSettings.h"
#include "SettingsStore.h"
#include <algorithm>
#include <cmath>

namespace editorGizmoSettings {
  float clampTranslateStep(const float step)
  {
    if (!std::isfinite(step))
    {
      return defaultTranslateStep;
    }

    return std::clamp(step, minTranslateStep, maxTranslateStep);
  }

  float clampRotateStepDegrees(const float step)
  {
    if (!std::isfinite(step))
    {
      return defaultRotateStepDegrees;
    }

    return std::clamp(step, minRotateStepDegrees, maxRotateStepDegrees);
  }

  float clampScaleStep(const float step)
  {
    if (!std::isfinite(step))
    {
      return defaultScaleStep;
    }

    return std::clamp(step, minScaleStep, maxScaleStep);
  }

  bool readSnapEnabled(const SettingsStore& settings)
  {
    return settings.get<bool>(snapEnabledKey, defaultSnapEnabled);
  }

  void writeSnapEnabled(SettingsStore& settings, const bool enabled)
  {
    settings.set(snapEnabledKey, enabled);
  }

  float readTranslateStep(const SettingsStore& settings)
  {
    return clampTranslateStep(settings.get<float>(translateStepKey, defaultTranslateStep));
  }

  void writeTranslateStep(SettingsStore& settings, const float step)
  {
    settings.set(translateStepKey, clampTranslateStep(step));
  }

  float readRotateStepDegrees(const SettingsStore& settings)
  {
    return clampRotateStepDegrees(settings.get<float>(rotateStepKey, defaultRotateStepDegrees));
  }

  void writeRotateStepDegrees(SettingsStore& settings, const float step)
  {
    settings.set(rotateStepKey, clampRotateStepDegrees(step));
  }

  float readScaleStep(const SettingsStore& settings)
  {
    return clampScaleStep(settings.get<float>(scaleStepKey, defaultScaleStep));
  }

  void writeScaleStep(SettingsStore& settings, const float step)
  {
    settings.set(scaleStepKey, clampScaleStep(step));
  }
}
