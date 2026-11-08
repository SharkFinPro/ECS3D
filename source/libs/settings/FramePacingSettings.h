#ifndef FRAMEPACINGSETTINGS_H
#define FRAMEPACINGSETTINGS_H

#include <optional>
#include <string_view>

class SettingsStore;

namespace framePacingSettings {
  inline constexpr bool defaultVsync = true;

  // Frames per second; 0 means unlimited.
  inline constexpr int unlimited = 0;
  inline constexpr int defaultMaxFps = unlimited;
  inline constexpr int minMaxFps = 10;
  inline constexpr int maxMaxFps = 1000;

  // The client and the editor each remember their own choice (they also use different files).
  struct Keys {
    const char* vsync;
    const char* maxFps;
  };

  inline constexpr Keys clientKeys { "client.display.vsync", "client.display.maxFps" };
  inline constexpr Keys editorKeys { "editor.display.vsync", "editor.display.maxFps" };

  // 0 or below -> unlimited; otherwise clamped into [minMaxFps, maxMaxFps].
  [[nodiscard]] int clampMaxFps(int framesPerSecond);

  [[nodiscard]] bool readVsync(const SettingsStore& settings, const Keys& keys);
  void writeVsync(SettingsStore& settings, const Keys& keys, bool vsync);

  // Clamped on read as well as on write, so a hand-edited file cannot ask for an out-of-range cap.
  // Infinity clamps like any huge value; NaN falls back to the default.
  [[nodiscard]] int readMaxFps(const SettingsStore& settings, const Keys& keys);
  void writeMaxFps(SettingsStore& settings, const Keys& keys, int framesPerSecond);

  // Parses a command-line cap: a whole decimal number, 0 for unlimited. Returns nullopt for anything else
  // (empty, sign, fraction, trailing characters) so the caller can reject the flag. A digits-only value is
  // returned clamped, including one too large for int (e.g. "5" -> 10, "100000" and "99999999999999999999" -> 1000).
  [[nodiscard]] std::optional<int> parseMaxFps(std::string_view text);

  // A command-line flag overrides the remembered preference for this launch and never changes it
  // (the same rule as the client's --windowed).
  [[nodiscard]] bool resolveVsync(const SettingsStore& settings, const Keys& keys, std::optional<bool> flag);
  [[nodiscard]] int resolveMaxFps(const SettingsStore& settings, const Keys& keys, std::optional<int> flag);
}

#endif  // FRAMEPACINGSETTINGS_H
