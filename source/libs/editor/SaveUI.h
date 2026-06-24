#ifndef SAVEUI_H
#define SAVEUI_H

#include <string>

class ProjectSerializer;

// The editor-only half of the old SaveManager: file dialogs + the Ctrl+S keybind + drag-and-drop
// loading. It drives the data-side ProjectSerializer once a path is chosen.
class SaveUI {
public:
  explicit SaveUI(ProjectSerializer* projectSerializer);

  void save();

  void saveAs();

  void open();

  void loadFromFile(const std::string& path);

  void createNewProject();

private:
  ProjectSerializer* m_projectSerializer;

  std::string m_saveFile;

  // TODO: migrate the nfd dialogs (SaveManager::chooseSaveFile / createSaveFile) and the window event
  // TODO:   listeners (registerWindowEvents: Ctrl+S -> save / Ctrl+Shift+S -> saveAs, drop event ->
  // TODO:   loadFromFile). Needs nfd linked into ECS3DEditorLib and access to the renderer's window.
  [[nodiscard]] bool chooseSaveFile();

  [[nodiscard]] bool createSaveFile();
};



#endif //SAVEUI_H
