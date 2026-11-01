#include "EditorLayoutSettings.h"
#include "SettingsStore.h"

namespace editorLayoutSettings {
  std::string readImGuiLayout(const SettingsStore& settings)
  {
    return settings.get<std::string>(imguiLayoutKey, std::string());
  }

  void writeImGuiLayout(SettingsStore& settings, const std::string& iniText)
  {
    settings.set(imguiLayoutKey, iniText);
  }

  bool hasDockLayout(const std::string_view iniText)
  {
    constexpr std::string_view dockSection = "[Docking][Data]";
    constexpr std::string_view dockSpaceLine = "DockSpace";

    bool inDockSection = false;
    size_t position = 0;

    while (position < iniText.size())
    {
      size_t end = iniText.find('\n', position);

      if (end == std::string_view::npos)
      {
        end = iniText.size();
      }

      std::string_view line = iniText.substr(position, end - position);
      position = end + 1;

      while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
      {
        line.remove_suffix(1);
      }

      if (line.empty())
      {
        continue;
      }

      if (line.front() == '[')
      {
        inDockSection = line == dockSection;
        continue;
      }

      if (inDockSection && line.substr(0, dockSpaceLine.size()) == dockSpaceLine)
      {
        return true;
      }
    }

    return false;
  }
}
