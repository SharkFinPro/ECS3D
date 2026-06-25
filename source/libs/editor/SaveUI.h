#ifndef SAVEUI_H
#define SAVEUI_H

#include <VulkanEngine/components/window/Window.h>
#include <functional>
#include <memory>
#include <string>

class ProjectSerializer;

namespace vke {
  class VulkanEngine;
}

// The editor-only half of the old SaveManager: native file dialogs + the Ctrl+S keybind + drag-and-drop
// loading. Save serializes the editor's replicated project straight to disk. Open/New don't load
// locally (that would desync from the authoritative server) — they hand the project blob to a callback
// the EditorApp uses to send a loadProject command, after which the server re-snapshots.
class SaveUI {
public:
  using LoadProjectCallback = std::function<void(const std::string& projectJson)>;

  SaveUI(ProjectSerializer* projectSerializer, std::shared_ptr<vke::VulkanEngine> renderer);

  ~SaveUI();

  void setLoadProjectCallback(LoadProjectCallback callback);

  void save();

  void saveAs();

  void open();

  void loadFromFile(const std::string& path);

  void createNewProject();

private:
  ProjectSerializer* m_projectSerializer;

  std::shared_ptr<vke::VulkanEngine> m_renderer;

  LoadProjectCallback m_onLoadProject;

  std::string m_saveFile;

  vke::EventListener<vke::KeyCallbackEvent> m_keyCallbackEventListener;
  vke::EventListener<vke::DropEvent> m_dropEventListener;

  [[nodiscard]] bool chooseSaveFile();

  [[nodiscard]] bool createSaveFile();

  void registerWindowEvents();
};



#endif //SAVEUI_H
