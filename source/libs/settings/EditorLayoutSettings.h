#ifndef EDITORLAYOUTSETTINGS_H
#define EDITORLAYOUTSETTINGS_H

#include <string>
#include <string_view>

class SettingsStore;

namespace editorLayoutSettings {
  inline constexpr const char* imguiLayoutKey = "editor.layout.imgui";
  inline constexpr const char* layoutVersionKey = "editor.layout.version";

  // Bump this whenever the default dock layout geometry changes. A layout stored by another version is ignored, so
  // the new defaults appear once and the next save replaces the old layout. A missing version reads as 0.
  inline constexpr int currentLayoutVersion = 1;

  // The ImGui ini text (window placement and dock tree); empty when none was saved.
  [[nodiscard]] std::string readImGuiLayout(const SettingsStore& settings);

  // Stamps the current layout version along with the text.
  void writeImGuiLayout(SettingsStore& settings, const std::string& iniText);

  [[nodiscard]] int readLayoutVersion(const SettingsStore& settings);

  // The stored layout when it was saved by the current version, otherwise empty.
  [[nodiscard]] std::string readRestorableImGuiLayout(const SettingsStore& settings);

  // True when the ini text holds a dock tree, so restoring it replaces the default dock layout.
  [[nodiscard]] bool hasDockLayout(std::string_view iniText);

  // A stored dock tree is never overwritten by text without one, so a save from an unready context cannot wipe it.
  [[nodiscard]] bool shouldReplaceLayout(std::string_view stored, std::string_view candidate);
}

#endif  // EDITORLAYOUTSETTINGS_H
