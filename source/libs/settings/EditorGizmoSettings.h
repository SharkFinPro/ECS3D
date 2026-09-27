#ifndef EDITORGIZMOSETTINGS_H
#define EDITORGIZMOSETTINGS_H

class SettingsStore;

// Snap settings for the viewport translate/rotate/scale gizmo. Plain values rather than a gizmo::Snap -
// settings must not depend on the editor lib (settings -> log + json only), so the caller builds the
// gizmo type from these.
namespace editorGizmoSettings {
  inline constexpr bool defaultSnapEnabled = true;

  inline constexpr float defaultTranslateStep = 0.5f;
  inline constexpr float minTranslateStep = 0.001f;
  inline constexpr float maxTranslateStep = 1000.0f;

  inline constexpr float defaultRotateStepDegrees = 15.0f;
  inline constexpr float minRotateStepDegrees = 0.1f;
  inline constexpr float maxRotateStepDegrees = 180.0f;

  inline constexpr float defaultScaleStep = 0.1f;
  inline constexpr float minScaleStep = 0.001f;
  inline constexpr float maxScaleStep = 10.0f;

  inline constexpr const char* snapEnabledKey = "editor.gizmo.snapEnabled";
  inline constexpr const char* translateStepKey = "editor.gizmo.translateStep";
  inline constexpr const char* rotateStepKey = "editor.gizmo.rotateStep";
  inline constexpr const char* scaleStepKey = "editor.gizmo.scaleStep";

  // A non-finite input (a hand-edited settings file) returns the matching default rather than a step
  // that would round every drag to zero or never snap at all.
  [[nodiscard]] float clampTranslateStep(float step);
  [[nodiscard]] float clampRotateStepDegrees(float step);
  [[nodiscard]] float clampScaleStep(float step);

  [[nodiscard]] bool readSnapEnabled(const SettingsStore& settings);
  void writeSnapEnabled(SettingsStore& settings, bool enabled);

  [[nodiscard]] float readTranslateStep(const SettingsStore& settings);
  void writeTranslateStep(SettingsStore& settings, float step);

  [[nodiscard]] float readRotateStepDegrees(const SettingsStore& settings);
  void writeRotateStepDegrees(SettingsStore& settings, float step);

  [[nodiscard]] float readScaleStep(const SettingsStore& settings);
  void writeScaleStep(SettingsStore& settings, float step);
}

#endif  // EDITORGIZMOSETTINGS_H
