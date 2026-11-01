#ifndef EDITORLAYOUTSETTINGS_H
#define EDITORLAYOUTSETTINGS_H

#include <string>
#include <string_view>

class SettingsStore;

namespace editorLayoutSettings {
  inline constexpr const char* imguiLayoutKey = "editor.layout.imgui";

  // The ImGui ini text (window placement and dock tree); empty when none was saved.
  [[nodiscard]] std::string readImGuiLayout(const SettingsStore& settings);
  void writeImGuiLayout(SettingsStore& settings, const std::string& iniText);

  // True when the ini text holds a dock tree, so restoring it replaces the default dock layout.
  [[nodiscard]] bool hasDockLayout(std::string_view iniText);

  // A stored dock tree is never overwritten by text without one, so a save from an unready context cannot wipe it.
  [[nodiscard]] bool shouldReplaceLayout(std::string_view stored, std::string_view candidate);
}

#endif  // EDITORLAYOUTSETTINGS_H
