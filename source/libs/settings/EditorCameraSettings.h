#ifndef EDITORCAMERASETTINGS_H
#define EDITORCAMERASETTINGS_H

class SettingsStore;

// The editor viewport's free-fly camera speed: a single multiplier vke::Camera::setSpeed scales
// movement, zoom and mouse-look sensitivity by together, so one setting tunes all three at once.
// Stored so navigation can be matched to a scene's scale without a rebuild.
namespace editorCameraSettings {
  // vke::EngineConfig::Camera's own default (see VulkanEngine's EngineConfig.h) - kept equal here so an
  // editor that has never touched the setting renders at the same speed it always has.
  inline constexpr float defaultSpeed = 1.0f;

  // Loose enough to still be useful at both a prop's scale and a terrain's, tight enough that neither end
  // is a de facto "broken" value.
  inline constexpr float minSpeed = 0.1f;
  inline constexpr float maxSpeed = 20.0f;

  inline constexpr const char* key = "editor.viewport.cameraSpeed";

  // Clamps to [minSpeed, maxSpeed]. A non-finite input (a hand-edited settings file) returns
  // defaultSpeed instead of reaching vke::Camera::setSpeed, which multiplies it straight into position.
  [[nodiscard]] float clampSpeed(float speed);

  [[nodiscard]] float readSpeed(const SettingsStore& settings);

  void writeSpeed(SettingsStore& settings, float speed);
}

#endif  // EDITORCAMERASETTINGS_H
